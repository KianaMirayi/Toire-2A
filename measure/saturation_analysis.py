# -*- coding: utf-8 -*-
"""
分析 UAD 的饱和特性

输出：
  - 每档电平的基波增益（看增益压缩）
  - 每档的 THD 与各次谐波
  - 拟合 tanh 模型所需的数据

关键：Peak = Min 时没有压缩，信号只经过「增益级 + 变压器 + 电子管」，
      所以测到的是纯饱和特性。
"""
import os
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

matplotlib.rcParams['font.sans-serif'] = ['Microsoft YaHei', 'SimHei', 'DejaVu Sans']
matplotlib.rcParams['axes.unicode_minus'] = False

BASE = r'E:\Programing\C++Hell\TOiRE-2A'
SIG = os.path.join(BASE, 'measure', 'signals', '08-sat-sweep.wav')
REN = os.path.join(BASE, 'measure', 'renders', 'sat')
REP = os.path.join(BASE, 'docs', 'reports')

SR = 48000
LEAD, SEG = 0.5, 1.5
LEVELS = list(range(-42, 1, 3))
LAT = 87


def read_wav(path):
    with open(path, 'rb') as f:
        if f.read(4) != b'RIFF':
            raise ValueError('not RIFF')
        f.seek(12)
        fmt, raw = None, None
        while True:
            h = f.read(8)
            if len(h) < 8:
                break
            c, s = h[:4], int.from_bytes(h[4:8], 'little')
            if c == b'fmt ':
                b = f.read(s)
                tag = int.from_bytes(b[0:2], 'little')
                nch = int.from_bytes(b[2:4], 'little')
                sr = int.from_bytes(b[4:8], 'little')
                bits = int.from_bytes(b[14:16], 'little')
                if tag == 0xFFFE and s >= 40:
                    tag = int.from_bytes(b[24:26], 'little')
                fmt = (tag, nch, sr, bits)
            elif c == b'data':
                raw = f.read(s)
            else:
                f.seek(s, 1)
    tag, nch, sr, bits = fmt
    if tag == 3:
        v = np.frombuffer(raw, dtype='<f4').astype(np.float64)
    elif bits == 24:
        a = np.frombuffer(raw, dtype=np.uint8)
        a = a[:len(a) // 3 * 3].reshape(-1, 3).astype(np.int32)
        x = a[:, 0] | (a[:, 1] << 8) | (a[:, 2] << 16)
        x = np.where(x >= (1 << 23), x - (1 << 24), x)
        v = x.astype(np.float64) / float(1 << 23)
    elif bits == 16:
        v = np.frombuffer(raw, dtype='<i2').astype(np.float64) / float(1 << 15)
    else:
        v = np.frombuffer(raw, dtype='<i4').astype(np.float64) / float(1 << 31)
    if nch > 1:
        v = v.reshape(-1, nch)[:, 0]
    return v, sr


def analyse_segment(x, a, b):
    y = x[int(a * SR):int(b * SR)]
    w = np.hanning(len(y))
    Y = np.abs(np.fft.rfft(y * w)) / (len(y) * 0.5)
    f = np.fft.rfftfreq(len(y), 1.0 / SR)

    def peak_at(f0):
        m = (f > f0 * 0.96) & (f < f0 * 1.04)
        return float(Y[m].max()) if m.any() else 1e-12

    fund = peak_at(1000.0)
    harms = [peak_at(1000.0 * h) for h in range(2, 11)]   # 2..10 次全部测（之前只测偶次，对称饱和会被漏掉）
    thd = np.sqrt(sum(h * h for h in harms)) / max(fund, 1e-12)
    return fund, thd, harms


fig, axes = plt.subplots(1, 3, figsize=(16, 5))

cases = [
    ('UAD-pmin-g50.wav', 'UAD  Peak=Min  Gain=50 (+14.5 dB)', '#c0392b'),
    ('UAD-pmin-g25.wav', 'UAD  Peak=Min  Gain=25 (+2.6 dB)',  '#e67e22'),
    ('OURS-pmin-g50.wav', '我们 Peak=Min  Gain=50 (+14.5 dB)', '#2980b9'),
]

print('%-34s %9s %9s %9s %9s' % ('配置', '输入dB', '基波dB', '增益dB', 'THD dB'))
print('-' * 76)

for fn, label, color in cases:
    p = os.path.join(REN, fn)
    if not os.path.exists(p):
        print('缺', fn)
        continue
    x, sr = read_wav(p)
    if 'UAD' in fn:
        x = np.concatenate([x[LAT:], np.zeros(LAT)])

    ins, gains, thds, hs_all = [], [], [], []
    for i, L in enumerate(LEVELS):
        a = LEAD + i * SEG + 0.2
        b = LEAD + (i + 1) * SEG
        fund, thd, harms = analyse_segment(x, a, b)
        fdb = 20 * np.log10(max(fund, 1e-12))
        # 基波幅度 vs 输入峰值：输入是正弦，峰值 = 10^(L/20)
        ins.append(L)
        gains.append(fdb - L)
        thds.append(20 * np.log10(max(thd, 1e-12)))
        hs_all.append([20 * np.log10(max(h / max(fund, 1e-12), 1e-12)) for h in harms])
        print('%-34s %9d %9.2f %+9.2f %9.1f' % (label if i == 0 else '', L, fdb, fdb - L, 20 * np.log10(max(thd, 1e-12))))

    axes[0].plot(ins, gains, 'o-', color=color, ms=3, lw=1.5, label=label)
    axes[1].plot(ins, thds, 'o-', color=color, ms=3, lw=1.5, label=label)
    h = np.array(hs_all)
    for k in range(3):
        axes[2].plot(ins, h[:, k], 'o-', color=color, ms=2.5, lw=1.2, alpha=.75,
                     label='%s H%d' % (label.split()[0], k + 2) if k == 0 else None)

for ax, title, ylab in [
    (axes[0], '基波增益 vs 输入电平\n（下降 = 饱和压缩）', '增益 (dB)'),
    (axes[1], 'THD vs 输入电平', 'THD (dB)'),
    (axes[2], '各次谐波 vs 输入电平', '谐波相对基波 (dB)'),
]:
    ax.set_xlabel('输入电平 (dBFS)')
    ax.set_ylabel(ylab)
    ax.set_title(title)
    ax.grid(alpha=.3)
    ax.legend(fontsize=7)

fig.tight_layout()
fig.savefig(os.path.join(REP, 'saturation-curve.png'), dpi=140)
plt.close(fig)
print('\n输出: docs/reports/saturation-curve.png')
