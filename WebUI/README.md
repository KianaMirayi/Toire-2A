# WebUI —— TOiRE-2A 的 WebView2 前端

LA-2A 风格的硬件拟真面板，跑在 JUCE 8 的 WebView2 里。
纯 HTML / CSS / JS，**没有 React、没有构建步骤、没有任何外网依赖**。

```
WebUI/
├── index.html      面板结构（固定 950x263，与 UAD LA-2A 同比例）
├── style.css       黑色拉丝面板、旋钮、VU 表、螺丝
├── juce-bridge.js  JUCE 8 滑块 relay 协议的最小实现（+ 浏览器预览 mock）
└── app.js          旋钮拖拽、VU 表指针、表头模式开关、遥测解析
```

---

## 一、怎么跑起来的

```
音频线程                        C++                                网页
─────────                     ─────                              ─────
processBlock()
   └─ uiBridge.publish(t)      Editor 30Hz Timer
                                 ├─ getUiBridge().snapshot()
                                 └─ emitEventIfBrowserIsVisible("telemetry", [gr, in, out, vu, frame])
                                                                     └─ __JUCE__.backend.addEventListener
                                                                          → 指针 + 数字

APVTS 参数  ←→  WebSliderRelay + WebSliderParameterAttachment  ←→  旋钮 / 表头开关（双向，可被 DAW 自动化）
```

- 参数名（必须与 C++ 里 `WebSliderRelay` 的构造参数完全一致）：
  `peakReduction`、`makeupGain`、`meterAttack`、`meterRelease`、`meterMode`
- `meterMode` 是 `AudioParameterChoice`（0/1/2），用 slider relay 走：
  归一化值 0 / 0.5 / 1 就是三档，写回用 `setNormalisedValue(mode / 2)`
- 遥测事件名：`telemetry`，载荷是 5 个数的数组
  `[gainReductionDb, inputPeakDb, outputPeakDb, vuDb, frame]`
- `frame` 不再变化 ⇒ 音频停了，前端自己平滑归零（不会冻住指针）

### 为什么不用官方 `juce-framework-frontend` 包

JUCE 8 的前端框架是 ES module，而 WebView2（尤其 DAW 里）对 `import` / importmap 不可靠。
`juce-bridge.js` 按 `juce_WebControlRelayEvents.h` 里的**同一套事件协议**重写了 ~120 行等价物，
所以页面可以只用传统 `<script src>` 标签。

它实现并挂到 `window.__JUCE__` 上的是：

```js
var st = window.__JUCE__.getSliderState('peakReduction');
st.valueChangedEvent.addListener(function () { /* 读 st.getScaledValue() */ });
st.setNormalisedValue(0.5);      // 推给 C++ / DAW
st.sliderDragStarted();          // 手势开始（宿主能记录自动化）
st.sliderDragEnded();
```

---

## 二、不启动插件也能看 UI

直接双击 `index.html`：`juce-bridge.js` 发现没有 `window.__JUCE__.backend` 时会装一个 mock
（假遥测 + 能用的旋钮），方便改样式。

也可以命令行截图（本机装了 Edge）：

```powershell
& "C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe" `
  --headless=new --disable-gpu --hide-scrollbars --window-size=950,263 `
  --force-device-scale-factor=2 --screenshot="$pwd\ui.png" `
  --virtual-time-budget=2500 "file:///E:/Programing/C++Hell/TOiRE-2A/WebUI/index.html"
```

> ⚠️ 用 `file:///` 打开时，WebView2 里的相对路径资源（css/js）能正常加载，
> 但**不要**改成 `http://localhost`：正式产物全部走本地文件，没有本地服务器。

---

## 三、构建 / 部署

```powershell
Remove-Item Env:HTTP_PROXY,Env:http_proxy,Env:HTTPS_PROXY,Env:https_proxy,`
           Env:ALL_PROXY,Env:all_proxy,Env:NO_PROXY,Env:no_proxy -ErrorAction SilentlyContinue

cmake -B build -S . -DTOIRE2A_WEBVIEW2=ON
cmake --build build --config Release
```

`CMakeLists.txt` 在 `TOIRE2A_WEBVIEW2=ON` 时会：

1. 加 `JUCE_WEB_BROWSER=1` / `JUCE_USE_WIN_WEBVIEW2=1` / `TOREI2A_HAS_WEBVIEW=1`
2. 链接 `juce::juce_gui_extra`，引入 NuGet WebView2 头文件
3. POST_BUILD：
   - `WebUI/` → `TOIRE2A_artefacts/Release/Standalone/webui/`
   - `WebUI/` → `TOIRE2A_artefacts/Release/VST3/TOIRE-2A.vst3/Contents/webui/`
   - `WebView2Loader.dll` → 上面两处
4. `COPY_PLUGIN_AFTER_BUILD` 再把整个 `.vst3`（含 `Contents/webui`）拷到
   `C:\Program Files\Common Files\VST3\`

C++ 侧找页面用 `findWebUiIndex()`：从 `File::invokedExecutableFile` 向上找
`webui/index.html`、`Contents/webui/index.html`，最后回退到源码树。

---

## 四、改 UI 时别踩的坑（都真踩过）

| 坑 | 现象 | 做法 |
|---|---|---|
| `setBounds()` 晚于 `addAndMakeVisible()` | WebView2 永远黑屏 | 顺序：setBounds → setOpaque → addAndMakeVisible → setVisible → toFront → goToURL |
| 构造期建 WebView | DAW 第一次加载崩 | `visibilityChanged()` 里首次可见才建 |
| userData 放 `%TEMP%` | 清 Temp 后冷启动变慢/失效 | `%APPDATA%/TOIRE-2A/WebView2` |
| 页面没加载完就 emit | 事件静默丢失 | `pageFinishedLoading` → `pageLoaded = true` |
| `evaluateJavascript` | DAW 里静默失败 | 只用 `emitEventIfBrowserIsVisible` |
| CDN / ES module / 动态 `<script>` | 白屏 | 全部本地文件 + 传统 `<script src>` |
| 用 `const` / 箭头函数 / `**` / `?.` | 老 Chromium 直接语法错 | 保守 ES5 写法 |
| 无数据时表头乱指 | 指针冻在某个值 | 用 `frame` 判断音频是否在跑，停了就归零 |

调试：WebView2 没有 DevTools。`index.html` 顶部有一条 `#errors` 红带，
`window.onerror` 和 `init()` 的 `try/catch` 都会把错误写进去 —— 白屏时先看它。

---

## 五、视觉说明

- 面板：950×263，近黑渐变（`#141416` → `#0a0a0b`）+ 细横纹拉丝 + 边缘金属高光 + 四角螺丝；
  文字用米白 `#d8d4c8`，点缀暗金 `#c8a05a`
- 布局照 UAD LA-2A：左上标牌 / 正中 VU 表 / 左下 LIMIT-COMPRESS 拨杆 / 右下 POWER 拨杆 /
  两只 104px 旋钮（中心 200,148 与 750,148）/ 右上 3 位表头开关 / 底部居中两条弹道窄条
- 旋钮：黑色滚花外环（约 4°/齿）+ 银色拉丝内盘 + 白色指针 + 白色 0–100 数字（照 ref-REV）
- VU 表：米黄表盘、-20…+3 不等距刻度、数字沿弧线旋转、末端 +1…+3 红带、
  细黑指针 1.5px、针轴贴面底；约 60% 面宽的弧（真表比例）
- 表头模式：Gain Reduction / Output +10 / Output +4，切换时表盘底部小字与数字窗标题一起变
- 指针映射在 `app.js` 的 `needleAngle()`：默认 `NEEDLE_MODE = "scale"`（按刻度插值，
  0 dB 落在刻度 0，>0 进红区）；改成 `"linear"` 就是"0 dB 顶到 +3"的线性映射

改颜色只动 `style.css` 的面板层和 `.knob-*` / `.meter-face` 几段；
改刻度/指针逻辑在 `app.js` 的 `drawDial()` / `drawMeterFace()` / `needleAngle()`。
