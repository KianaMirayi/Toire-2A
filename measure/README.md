# LA-2A 测量流程

> 目的：用**测量**反推 UAD LA-2A 的行为参数，用来校准 TOiRE-2A 的常数。
> 分工：你在 DAW 里跑渲染 → 我分析导出的 WAV。

---

## 一、原理：为什么要这么测

```
结构（有哪几个状态、什么拓扑）  ← 电路图（已完成）
参数（时间常数、曲线形状）      ← 测量（本流程）
```

**恒定输入 + 观察输出 = 直接看到增益的变化。**

因为输入电平已知且不变，输出包络的每一处变化都只来自压缩器的增益变化：
`GR(t) = 20·log10( v_out(t) / v_in )`

不需要读插件内部任何状态。

---

## 二、准备

### 测试信号（已生成）

`measure/signals/` 下 5 个 WAV，48 kHz / 24-bit / 单声道：

| 文件 | 长度 | 内容 | 测什么 |
|---|---|---|---|
| `01-io-stairs.wav` | 32.0 s | 1 kHz 正弦，−60→0 dBFS，3 dB 一档，每档 1.5 s | 静态 IO 曲线 |
| `02-release.wav` | 22.8 s | 过载 500 ms / 20 ms / 2 s，各接 6–8 s 静音 | release 两段衰减 + program dependent |
| `03-attack.wav` | 9.2 s | 硬起音 −6/−12/−20 dBFS | attack 时间常数 |
| `04-sidechain-freq.wav` | 12.3 s | 100/200/500/1k/2k/4k/8k/12k Hz，同电平 | 侧链预加重（R37） |
| `05-tone.wav` | 10.5 s | 1 kHz + 100 Hz 纯音 @ −6 dBFS | 谐波染色 |

**不要改动这些文件**（增益、裁剪、重采样都不行）—— 分析脚本按固定布局解析。

---

## 三、在 Fender Studio 里怎么做

### 每个参数组合 = 一次渲染

```
1. 新建工程，采样率 48 kHz
2. 导入一个测试 WAV 到一条轨道
3. 该轨道插入 UADx LA-2A
4. 设好参数（见下表）
5. 导出整轨 → WAV / 48 kHz / 24-bit 或 32-bit float
6. 文件名按规范命名
```

### ⚠️ 导出时必须关闭的处理

| 必须关闭 | 原因 |
|---|---|
| **Normalize（归一化）** | 会破坏绝对电平 |
| **Fade in/out（淡入淡出）** | 会破坏起音和 release 测量 |
| **Master 限制器 / 总线压缩** | 会污染输出 |
| 任何 EQ / 饱和 / 抖动 | 会改变频谱和包络 |
| 改变采样率 | 布局对不上 |

**正确做法**：干信号导入时电平不动 → 插件 → 导出时电平也不动。

### 参数设置：第一批只做两组

| 渲染编号 | Peak Reduction | Gain | Comp/Limit | 用哪些信号 |
|---|---|---|---|---|
| **R1** | 0.50（12 点方向） | 固定某个位置（记下来） | **Comp** | 全部 5 个 |
| **R2** | 0.50 | 同上，**完全一致** | **Limit** | 全部 5 个 |

> Gain 只要两次渲染一致就行，具体值不重要 —— 我会从最低电平档（−60 dBFS）反推它。

第二批（等第一批结果出来再定）：

| **R3** | 0.25 | 同 R1 | Comp | 01 / 02 |
| **R4** | 0.75 | 同 R1 | Comp | 01 / 02 |
| **R5** | 0.00（最小） | 同 R1 | Comp | 01 |

**一次只改一个参数。**

---

## 四、文件命名

```
R1-comp--01-io-stairs.wav
R1-comp--02-release.wav
R2-limit--01-io-stairs.wav
...
R4-peak075-comp--02-release.wav
```

命名里的 `R1` `comp` `peak075` 让我知道参数组合。

**建议放在**：`E:\Programing\C++Hell\TOiRE-2A\measure\renders\`

---

## 五、我这边怎么分析

```bash
python measure/analyze.py io      --dry <干> --wet <湿>
python measure/analyze.py release --dry <干> --wet <湿>
python measure/analyze.py attack  --dry <干> --wet <湿>
python measure/analyze.py freq    --dry <干> --wet <湿>
```

脚本会自动：
1. 读取 24-bit / 32-bit WAV
2. **用互相关对齐延迟**（DAW 可能引入延迟，自动处理）
3. 按固定布局切分信号
4. 输出每段的 RMS 电平和增益变化

### 自检已通过

用干信号自己当湿信号：

```
输入dB      干RMS     湿RMS    增益变化    GR估计
-60       -63.01   -63.01     +0.00     -0.00
...
0          -3.01    -3.01     +0.00     -0.00
```

完美 1:1，0 dB 变化 —— 链路可信。

（注：正弦的 RMS 比峰值低 3 dB，所以 −60 dBFS 峰值对应 −63 dB RMS，正常。）

---

## 六、分析产出

| 实验 | 输出 |
|---|---|
| **IO 曲线** | 表格 + 拐点/斜率 → 反推 threshold 映射、γ、kRMin |
| **Release** | 每毫秒的衰减表 → 快段/慢段时间常数 + program dependent 规律 |
| **Attack** | 起音后 0.5–120 ms 的差值 → attack 时间常数 |
| **Freq** | 各频率的 GR → 侧链预加重曲线 |
| **Tone** | 我会再做 FFT 看谐波 |

---

## 七、先做哪一步

**建议先只做 R1 的 `01-io-stairs.wav` 和 `02-release.wav`** —— 两个文件，
一次渲染就能同时拿到：

- 静态压缩曲线（threshold / ratio / knee）
- release 快段和长尾

拿到这两个，就能把代码里最关键的 4 个常数定下来。
