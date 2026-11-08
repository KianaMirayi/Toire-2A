# measure/bin —— 测量工具

自研 VST3 工具，**不依赖 DAW**。两个 exe，功能不重叠。

---

## rea-measure.exe —— 测量宿主（带实时日志区）

**打开就是日志窗口**，处理音频时实时滚动打印电平和增益。

```
双击「测量 UAD LA-2A.bat」            ← 直接跑一遍 UAD 测量
```

命令行三种模式：

```powershell
# 1) 列出插件参数（带显示文本）
rea-measure.exe --list-params --plugin "<vst3>"

# 2) 扫描参数映射（归一化值 → 显示文本）
rea-measure.exe --sweep 0 --plugin "<vst3>"

# 3) 处理音频（实时日志 + 写出 WAV）
rea-measure.exe --plugin "<vst3>" --in input.wav --out output.wav `
                --param 0=0.5 --param 1=0.5 --param 2=0 --param 4=1 `
                --log run.txt
```

> ⚠️ 路径必须加引号（`Program Files` 有空格）

### 参数索引（UADx LA-2A）

| 索引 | 名称 | 说明 |
|---|---|---|
| 0 | Peak Reduct | 0–100 拨盘（0.05→0，0.95→100） |
| 1 | Gain | 0–100 拨盘 |
| 2 | Comp/Limit | 0 = Comp，1 = Limit |
| 3 | Meter | 0 = +10dB，0.5 = GR，1 = +4dB |
| 4 | Power | 1 = On |
| 5 | Master Bypass | 1 = On |

插件自报延迟 **87 采样**。

---

## rea-viewer.exe —— 看插件界面

```
双击「看 UAD LA-2A.bat」            ← 打开 UAD LA-2A 的界面
rea-viewer.bat "C:\...\xxx.vst3"   ← 打开任意插件
把 .vst3 拖到 rea-viewer.bat 上
```

---

## 完整测量流程

```powershell
$bin = "E:\Programing\C++Hell\TOiRE-2A\measure"
$uad = "C:\Program Files\Common Files\VST3\uaudio_teletronix_la-2a_tc.vst3\Contents\x86_64-win\uaudio_teletronix_la-2a_tc.vst3"

# 1. 渲染（窗口里能实时看到电平）
& "$bin\bin\rea-measure.exe" --plugin $uad `
    --in  "$bin\signals\01-io-stairs.wav" `
    --out "$bin\renders\R1-comp--01.wav" `
    --param 0=0.5 --param 1=0.5 --param 2=0 --param 4=1

# 2. 分析
python "$bin\analyze.py" io --dry "$bin\signals\01-io-stairs.wav" --wet "$bin\renders\R1-comp--01.wav" --latency 87

# 3. 出图 + CSV
python "$bin\plot.py"
```

---

## 踩过的坑（改代码时注意）

| 坑 | 后果 | 正确做法 |
|---|---|---|
| 参数在后台线程设置 | UAD 插件静默不生效（增益变成 0 dB） | 在**消息线程**创建插件、设参数、prepareToPlay |
| RMS 按多声道平均 | 输入单声道/输出立体声时凭空掉 3 dB（曾把 +14.50 算成 +8.48） | 只看**第 0 声道** |
| `String::formatted("%s", utf8)` | Windows 上 UTF-8 被当本地编码，参数名乱码 | 用 `String <<` 拼接 |
| `.bat` 里写中文注释 | cmd 按 GBK 解码，字节 0x5C 吃掉下一行 | `.bat` 内容只用 ASCII |
| 控制台程序显示 WebView2 编辑器 | 访问冲突 (0xC0000005, ntdll) | 改用 GUI 子系统应用 |

---

## 源码

```
measure/host/main.cpp        rea-measure（GUI + 实时日志）
measure/host/viewer.cpp      rea-viewer
measure/host/CMakeLists.txt
```

重新编译：

```powershell
cd E:\Programing\C++Hell\TOiRE-2A\measure\host
cmake -B build -G "Visual Studio 18 2026" -A x64
cmake --build build --config Release
```
