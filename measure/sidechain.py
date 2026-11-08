# -*- coding: utf-8 -*-
"""
侧链预加重（R37）测量

原理：8 个频率以同一电平（−12 dBFS）送入，压缩器对每个频率的反应强度不同
     → GR 随频率的变化 = 侧链网络的频率响应

要求音频通路平坦（LA-2A 规格 ±0.1 dB 30 Hz–15 kHz），所以
     gainChange(f) 的差异基本就是侧链灵敏度的差异

输出：每个频率的 GR，以及相对 1 kHz 的归一化响应
"""
import os
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

matplotlib.rcParams['font.sans-serif'] = ['Microsoft YaHei', 'SimHei', 'DejaVu Sans']
matplotlib.rcParams['axes.unicode_minus'] = False

BASE = r'E:\Programing\C++Hell\TOiRE-2A'
SIG = os.path.join(BASE, 'measure', 'signals', '04-sidechain-freq.wav')
SC = os.path.join(BASE, 'measure', 'renders', 'sc')
REP = os.path.join(BASE, 'docs', 'reports')
os.makedirs(REP, exist_ok=True)

SR = 48000
LEAD = 0.3
SEG = 1.5
FREQS = [100, 200, 500, 1000, 2000, 4000, 8000, 12000]
SETTLE = 0.9        # 每段只取后 0.9 s（等压缩状态稳定）

CASES = [
    ('UAD--r37fixed.wav', 'UAD (R37 固定)',   87),
    ('RF--r37_000.wav',   'RF-2A  R37 = 0%',   0),
    ('RF--r37_025.wav',   'RF-2A  R37 = 25%',  0),
    ('RF--r37_050.wav',   'RF-2A  R37 = 50%',  0),
    ('RF--r37_075.wav',   'RF-2A  R37 = 75%',  0),
    ('RF--r37_100.wav',   'RF-2A  R37 = 100%', 0),
]


def read_wav(path):
    with open(path, 'rb') as f:
        if f.read(4) != b'RIFF':
            raise ValueError('not RIFF')
        f.seek(12)
        fmt, raw = None, None
        while True:
            hdr = f.read(8)
            if len(hdr) < 8:
                break
            cid, sz = hdr[:4], int.from_bytes(hdr[4:8], 'little')
            if cid == b'fmt ':
                b = f.read(sz)
                tag = int.from_bytes(b[0:2], 'little')
                nch = int.from_bytes(b[2:4], 'little')
                sr = int.from_bytes(b[4:8], 'little')
                bits = int.from_bytes(b[14:16], 'little')
                if tag == 0xFFFE and sz >= 40:
                    tag = int.from_bytes(b[24:26], 'little')
                fmt = (tag, nch, sr, bits)
            elif cid == b'data':
                raw = f.read(sz)
            else:
                f.seek(sz, 1)
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


def hp(x, sr, fc=30.0):
    n = len(x)
    X = np.fft.rfft(x)
    f = np.fft.rfftfreq(n, 1.0 / sr)
    return np.fft.irfft(X * (1.0 / (1.0 + (fc / np.maximum(f, 1e-6)) ** 4)), n)


def seg_db(x, t0, t1, sr):
    s, e = int(t0 * sr), int(t1 * sr)
    y = x[s:e]
    return 20 * np.log10(max(np.sqrt(np.mean(y ** 2)), 1e-12))


dry, sr = read_wav(SIG)
dry = hp(dry, sr)

fig, ax = plt.subplots(figsize=(10, 6.5))
colors = ['#c0392b', '#2980b9', '#27ae60', '#8e44ad', '#e67e22', '#16a085']
summary = []

print('%-20s' % '配置' + ''.join('%9s' % ('%dHz' % f) for f in FREQS))
print('-' * (20 + 9 * len(FREQS)))

for ci, (fname, label, lat) in enumerate(CASES):
    p = os.path.join(SC, fname)
    if not os.path.exists(p):
        print('  缺少', fname)
        continue
    wet, _ = read_wav(p)
    wet = hp(wet, sr)
    if lat > 0:
        wet = np.concatenate([wet[lat:], np.zeros(lat)])
    n = min(len(dry), len(wet))
    dry_n, wet_n = dry[:n], wet[:n]

    grs = []
    for i, f in enumerate(FREQS):
        a = LEAD + i * SEG
        b = a + SEG
        d = seg_db(dry_n, a + (SEG - SETTLE), b, sr)
        w = seg_db(wet_n, a + (SEG - SETTLE), b, sr)
        grs.append(w - d)          # 增益变化（负 = 压得更多）
    grs = np.array(grs)

    # 归一化到 1 kHz
    ref = grs[FREQS.index(1000)]
    rel = grs - ref
    summary.append((label, rel))

    print('%-20s' % label + ''.join('%9.2f' % v for v in rel))
    ax.plot(FREQS, rel, 'o-', color=colors[ci % len(colors)], ms=4, lw=1.6, label=label)

ax.set_xscale('log')
ax.set_xticks(FREQS)
ax.set_xticklabels(['%d' % f for f in FREQS])
ax.set_xlabel('频率 (Hz)')
ax.set_ylabel('相对 1 kHz 的额外增益衰减 (dB)')
ax.set_title('侧链预加重 (R37) —— 压缩灵敏度随频率的变化\n（越往下 = 该频率触发越多压缩）')
ax.grid(alpha=.3, which='both')
ax.axhline(0, color='#999', lw=.8, ls='--')
ax.legend(fontsize=8)
fig.tight_layout()
fig.savefig(os.path.join(REP, 'sidechain-r37.png'), dpi=140)
plt.close(fig)

print('\n（数值 = 相对 1 kHz 的额外 GR，负值表示该频率让压缩器更敏感）')
print('输出: docs/reports/sidechain-r37.png')
