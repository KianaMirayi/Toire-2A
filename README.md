# TOiRE-2A

LA-2A 风格光电式电平控制器（JUCE 8 插件，VST3 + Standalone）。

> 状态：**原型 0.1** — 项目骨架已可编译运行，DSP 为行为模型初版，常数待用 RF-2A 实测校准。

---

## 目录结构

```
TOiRE-2A/
├── CMakeLists.txt              # JUCE 8.0.7，VS 2026，VST3 + Standalone
├── CMakePresets.json           # vs2026 / vs2026-web
├── docs/
│   └── LA-2A-implementation-notes.md   # 电路原理与建模笔记（先读这个）
├── src/
│   ├── PluginProcessor.h/.cpp  # 参数、processBlock、遥测发布
│   ├── PluginEditor.h/.cpp     # 原生 UI（当前阶段）
│   ├── Bridge/
│   │   └── UiBridge.h          # DSP → UI 无锁桥（前端无关）
│   └── DSP/
│       └── LA2ACompressor.h/.cpp   # T4 行为模型
├── WebUI/
│   └── README.md               # 切换 WebView2 前端的步骤
└── Resources/                  # 预留给 Web 前端产物
```

---

## 构建

依赖：Visual Studio 2026（MSVC 19.51）、CMake ≥ 3.22、JUCE 在 `../JUCE`（8.0.7）。

### 命令行

```bash
cd E:/Programing/C++Hell/TOiRE-2A
cmake --preset vs2026
cmake --build build --config Debug
```

只编 Standalone：

```bash
cmake --build build --config Debug --target TOIRE2A_Standalone
```

产物：`build/TOIRE2A_artefacts/Debug/Standalone/TOIRE-2A.exe`
VST3 会自动安装到 `C:\Program Files\Common Files\VST3\TOIRE-2A.vst3`

### VSCode

> ⚠️ **坑：VSCode 默认终端是 PowerShell，而 `vcvarsall.bat` 是 cmd 语法。**
> 直接在 tasks.json 里写 `command: "cmd"` + 带引号的路径会被 PowerShell 二次解析，
> 报 `'C:\Program' 不是内部或外部命令`。
>
> **解决：构建逻辑放进 `scripts/*.bat`，任务只负责调脚本**（已实现）。

仓库已带 `.vscode/` 和 `scripts/`，打开文件夹即可用：

| 操作 | 方式 |
|---|---|
| 构建（默认） | `Ctrl+Shift+B` |
| 其它构建任务 | `Ctrl+Shift+P` → `Tasks: Run Task` |
| 调试 | `F5`（自动先构建 Standalone） |
| 智能提示 | 由 CMake Tools 提供，首次打开会自动 configure |

任务列表：

- `CMake: configure (MSVC 2026)` → `scripts/configure.bat`
- `CMake: build (MSVC 2026)` ← 默认构建任务（VST3 + Standalone）
- `CMake: build Standalone only (fast)` ← 只编 exe，最快
- `CMake: build Release`
- `CMake: configure + build`

命令行等价写法：

```bat
scripts\configure.bat
scripts\build.bat
scripts\build.bat Debug TOIRE2A_Standalone
scripts\build.bat Release
```

需要的扩展（`extensions.json` 会提示）：`ms-vscode.cmake-tools`、`ms-vscode.cpptools`。

> 即使 CMake Tools 不认 `Visual Studio 18 2026` 生成器也没关系 —— 任务走的是
> `scripts/build.bat`，自己 `call vcvarsall`，完全不依赖扩展。


### 首次运行

Standalone 第一次启动会弹 **Audio/MIDI Settings** 对话框选声卡，关掉后就是插件主界面。

---

## 架构要点

**三层分离，DSP 不认识 UI：**

```
LA2ACompressor (纯 DSP)
      │
      ▼  UiTelemetry
   UiBridge  (std::atomic，无锁)
      │
      ├──► 原生 Editor（现在）
      └──► WebView Editor（以后）
```

- `LA2ACompressor` 不 include 任何 UI 头
- `UiBridge` 是唯一出口，`toWebPayload()` 已备好 WebView 用的载荷
- 换前端只改 `PluginProcessor::createEditor()` 一行，见 `WebUI/README.md`

---

## DSP 参数

| 参数 | 范围 | 说明 |
|---|---|---|
| Peak Reduction | 0..1 | 映射到 -30..0 dB 阈值 |
| Gain | -24..+30 dB | 补偿增益 |

内部 T4 模型常数（`LA2ACompressor.cpp` 顶部）：

| 常数 | 当前值 | 依据 |
|---|---|---|
| `kAttackMs` | 10 ms | UA 官方手册（固定攻击时间） |
| `kReleaseMs` | 60 ms | 50% 释放时间 |
| `kTailMs` | 450 ms | 余辉长尾（待校准） |
| `kGamma` | 0.7 | CdS 光照→电阻指数（待校准） |
| `kRMin` | 0.02 | 最小电阻（最大压缩量） |

> ⚠️ 这些是**文献共识值**，不是从 RF-2A 反推的。校准方法见
> `docs/LA-2A-implementation-notes.md` 第 7 节。

---

## 已知限制

- 未实现侧链预加重（高频更敏感 / 压缩越深越暗）
- 未实现变压器与电子管染色
- 未做过采样（当前非线性不重，暂不需要）
- release 只有两段（EL + 余辉），真实 T4 是多段
