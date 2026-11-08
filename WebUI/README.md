# WebUI 前端接入说明（预留）

当前阶段 `TOiRE-2A` 用**原生 JUCE UI**（`src/PluginEditor.*`）。
这个目录存在的目的：**保证以后换成 WebView2 + Web UI 时，DSP 和 Bridge 一行都不用改。**

---

## 一、架构约定（已经实现）

```
音频线程                    桥（无锁原子量）                  UI 线程
─────────                  ─────────────────                ─────────
processBlock()  ──publish──►  UiBridge  ──snapshot()──►  原生 Editor (Timer 30Hz)
                                        └──snapshot()──►  WebEditor  (Timer 30Hz)
                                                              │
                                                              └─ emitEventIfBrowserIsVisible
                                                                 →  __JUCE__.backend.addEventListener
```

关键点：

1. **DSP 不认识 UI** — `LA2ACompressor` 只处理音频，不 include 任何 UI 头。
2. **Bridge 是唯一的出口** — `src/Bridge/UiBridge.h` 定义了 `UiTelemetry` 和 `publish/snapshot`。
3. **两个前端共用同一份数据** — 原生 Editor 和未来的 WebEditor 都调 `processor.getUiBridge().snapshot()`。
4. **载荷构造已经写好** — `UiBridge::toWebPayload(t)` 返回 `juce::Array<juce::var>`，
   可以直接丢给 `emitEventIfBrowserIsVisible`。

---

## 二、切换步骤（三步）

### 步骤 1：打开 CMake 选项

```bash
cmake -B build -G "Visual Studio 18 2026" -A x64 -DTOIRE2A_WEBVIEW2=ON
```

或者直接用预设：

```bash
cmake --preset vs2026-web
cmake --build build --config Debug
```

打开后 CMakeLists 会自动：

- 加 `JUCE_WEB_BROWSER=1` / `JUCE_USE_WIN_WEBVIEW2=1` / `TOREI2A_HAS_WEBVIEW=1`
- 引入 NuGet 里的 WebView2 头文件
- 链接 `juce::juce_gui_extra`

### 步骤 2：加 Web 版 Editor

新建 `src/WebEditor.h/.cpp`，结构参照 TOREI-EQ 的 `PluginEditor`：

```cpp
class TOIRE2AWebEditor : public juce::AudioProcessorEditor,
                         private juce::Timer
{
public:
    explicit TOIRE2AWebEditor (TOIRE2AAudioProcessor& p);

private:
    void timerCallback() override
    {
        const auto t = processor.getUiBridge().snapshot();

        // 音频停了就跳过，避免刷无效帧
        if (t.frame == lastFrame) return;
        lastFrame = t.frame;

        // ⚠️ 用 emitEventIfBrowserIsVisible，不要用 evaluateJavascript
        webView->emitEventIfBrowserIsVisible (
            "relicData", toreia2a::UiBridge::toWebPayload (t));
    }

    TOIRE2AAudioProcessor& processor;
    std::unique_ptr<juce::WebBrowserComponent> webView;
    std::uint32_t lastFrame = 0;
};
```

JS 侧：

```js
__JUCE__.backend.addEventListener('relicData', function (d) {
    // d = [gainReductionDb, inputPeakDb, outputPeakDb, vuDb]
    drawMeter(d[0]);
});
```

### 步骤 3：换一行

`src/PluginProcessor.cpp` 里 `createEditor()`：

```cpp
juce::AudioProcessorEditor* TOIRE2AAudioProcessor::createEditor()
{
#if TOREI2A_HAS_WEBVIEW
    return new TOIRE2AWebEditor (*this);          // ← 只改这里
#else
    return new TOIRE2AAudioProcessorEditor (*this);
#endif
}
```

**DSP 和 Bridge 不需要任何改动。**

---

## 三、接 WebView 时必须遵守的几条（来自 TOREI-EQ 的踩坑记录）

| 坑 | 正确做法 |
|---|---|
| 黑屏 | `setBounds()` **必须在** `addAndMakeVisible()` **之前** |
| 背景闪白 | `withBackgroundColour` = `paint()` 填充色 = HTML `body` 背景色（都用 `#1a1a1e`） |
| C++ → JS 不通 | 用 `emitEventIfBrowserIsVisible()`，**不要** `evaluateJavascript()` |
| JS → C++ 方向搞反 | 监听用 `__JUCE__.backend.addEventListener()`，发送才用 `.emitEvent()` |
| 路径含 `+` 加载失败 | 项目路径 `C++Hell` 含 `+` —— 用 Base64 UserScript 注入，别用 `file:///` |
| 首次加载崩 DAW | WebView 延迟到 `visibilityChanged()` 首次可见时创建；userData 放 `%APPDATA%` 不放 `%TEMP%` |

这套已经在 `TOREI-EQ` / `TOIRE-LevelMeter` 上验证过，直接照抄即可。

---

## 四、参数绑定（自动化）

参数已经走 `APVTS`，所以 Web 侧可以直接用 JUCE 8 的标准 API：

```cpp
juce::WebSliderRelay peakRelay { "peakReduction" };
juce::WebSliderParameterAttachment peakAttachment {
    peakRelay, *processor.apvts.getParameter ("peakReduction"), nullptr };

const auto opts = juce::WebBrowserComponent::Options{}
    .withBackend (juce::WebBrowserComponent::Options::Backend::webview2)
    .withOptionsFrom (peakRelay);     // ⚠️ withBackend 返回新对象，必须接住
```

JS 侧：

```js
var st = window.__JUCE__.getSliderState('peakReduction');
st.valueChangedEvent.addListener(function (v) { updateKnob(v); });
```

---

## 五、当前占位状态

- `Resources/webui/` — 空，留给以后的前端产物
- `WebUI/` — 本说明文件
- 未来通过 CMake POST_BUILD 把 `Resources/webui/` 复制到 VST3 的 `Contents/webui/`
