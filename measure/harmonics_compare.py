# -*- coding: utf-8 -*-
"""
谐波测量（新增）—— 对比 UAD 与 TOiRE-2A 的 H2..H7

两个条件：
  ① Peak = Min（纯饱和路径，看染色本身）
  ② Peak = 50（压缩工作状态，实际听音条件）

信号：08-sat-sweep.wav（1 kHz 纯正弦，−42→0 dBFS 三档一步）
"""
import os
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

matplotlib.rcParams['font.sans-serif'] = ['Microsoft YaHei', 'SimHei', 'DejaVu Sans']
matplotlib.rcParams['axes.unicode_minus'] = False

BASE = r'E:\Programing\C++Hell\TOiRE-2A'
REN = os.path.join(BASE, 'measure', 'renders', 'sat')
REP = os.path.join(BASE, 'docs', 'reports')
SR = 48000
LEAD, SEG = 0.5, 1.5
LEVELS = list(range(-42, 1, 3))
LAT = 87


def read_wav(path):
    with open(path, 'rb') as f:
        if f.read(4) != b'RIFF':
            raise ValueError(path)
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
    else:
        v = np.frombuffer(raw, dtype='<i4').astype(np.float64) / float(1 << 31)
    if nch > 1:
        v = v.reshape(-1, nch)[:, 0]
    return v, sr


def harmonics(x, a, b):
    y = x[int(a * SR):int(b * SR)]
    w = np.hanning(len(y))
    Y = np.abs(np.fft.rfft(y * w)) / (len(y) * 0.5)
    f = np.fft.rfftfreq(len(y), 1.0 / SR)

    def pk(f0):
        m = (f > f0 * 0.96) & (f < f0 * 1.04)
        return float(Y[m].max()) if m.any() else 1e-12

    fund = pk(1000.0)
    return 20 * np.log10(max(fund, 1e-12)), \
           [20 * np.log10(max(pk(1000.0 * h) / fund, 1e-12)) for h in range(2, 8)]


CASES = [
    ('UAD-pmin-g50.wav', 'UAD-pmin'),
    ('OURS-pmin-g50.wav', 'OURS-pmin'),
    ('UAD-p50.wav', 'UAD-p50'),
    ('OURS-p50.wav', 'OURS-p50'),
]

data = {}
for fn, key in CASES:
    p = os.path.join(REN, fn)
    if not os.path.exists(p):
        print('缺', fn)
        continue
    x, _ = read_wav(p)
    if key.startswith('UAD'):
        x = np.concatenate([x[LAT:], np.zeros(LAT)])
    rows = {}
    for i, L in enumerate(LEVELS):
        fund, hs = harmonics(x, LEAD + i * SEG + 0.2, LEAD + (i + 1) * SEG)
        rows[L] = hs
    data[key] = rows

# ── 表格 ────────────────────────────────────────────────
def show(uadkey, ourkey, title):
    if uadkey not in data or ourkey not in data:
        print('\n[%s] 数据不全，跳过' % title)
        return
    print('\n=== %s ===' % title)
    print('%8s %s | %s' % ('输入', ''.join('%8s' % ('UH%d' % h) for h in range(2, 8)),
                           ''.join('%8s' % ('OH%d' % h) for h in range(2, 8))))
    for L in LEVELS:
        if L < -27:
            continue
        u, o = data[uadkey][L], data[ourkey][L]
        print('%8d %s | %s' % (L, ''.join('%8.1f' % v for v in u),
                               ''.join('%8.1f' % v for v in o)))
    print('%8s %s' % ('误差', ''.join('%+8.1f' % (o[k] - u[k]) for k in range(6))))
    errs = [abs(data[ourkey][L][k] - data[uadkey][L][k])
            for L in LEVELS if L >= -27 for k in range(6)]
    print('  平均绝对误差: %.2f dB' % np.mean(errs))

show('UAD-pmin', 'OURS-pmin', 'Peak = Min（纯饱和路径）')
show('UAD-p50', 'OURS-p50', 'Peak = 50（压缩工作状态）')

# ── 图 ─────────────────────────────────────────────────
fig, axes = plt.subplots(1, 2, figsize=(14, 5))
for ax, (uk, ok, title) in zip(axes, [
        ('UAD-pmin', 'OURS-pmin', 'Peak = Min（纯饱和）'),
        ('UAD-p50', 'OURS-p50', 'Peak = 50（压缩状态）')]):
    if uk not in data or ok not in data:
        continue
    for k, h in enumerate(range(2, 8)):
        xs = [L for L in LEVELS if L >= -30]
        ax.plot(xs, [data[uk][L][k] for L in xs], '-o', ms=2.5, lw=1.3,
                color='#c0392b', alpha=.85,
                label='UAD H%d' % h if k < 3 else None)
        ax.plot(xs, [data[ok][L][k] for L in xs], '--s', ms=2.5, lw=1.3,
                color='#f39c12', alpha=.85,
                label='我们 H%d' % h if k < 3 else None)
    ax.set_xlabel('输入电平 (dBFS)')
    ax.set_ylabel('谐波相对基波 (dB)')
    ax.set_title('H2/H3/H4 —— ' + title)
    ax.grid(alpha=.3)
    ax.legend(fontsize=7, ncol=2)
    ax.set_ylim(-120, 0)
fig.tight_layout()
fig.savefig(os.path.join(REP, 'harmonics-compare.png'), dpi=140)
plt.close(fig)
print('\n输出: docs/reports/harmonics-compare.png')
