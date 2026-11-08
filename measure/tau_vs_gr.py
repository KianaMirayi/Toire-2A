# -*- coding: utf-8 -*-
"""
UAD 的「瞬时时间常数 τ vs 当前 GR」完整曲线

做法：对每个 Peak 位置，用 500 ms 过载后的释放曲线逐段反推
      τ = Δt / ln(GR₁/GR₂)，然后画 τ 对 GR 的关系。

判据：
  · 若 τ = A/GR（双曲线律）→ τ·GR 应该是【水平线】
  · 若不是 → 能看出真实规律

同时画 TOiRE-2A 的对照。
"""
import os
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

matplotlib.rcParams['font.sans-serif'] = ['Microsoft YaHei', 'SimHei', 'DejaVu Sans']
matplotlib.rcParams['axes.unicode_minus'] = False

BASE = r'E:\Programing\C++Hell\TOiRE-2A'
REN = os.path.join(BASE, 'measure', 'renders', 'relpeak')
REP = os.path.join(BASE, 'docs', 'reports')
SR = 48000
LEAD = 0.5
PEAKS = [('000', 0), ('010', 10), ('020', 20), ('030', 30), ('040', 40),
         ('050', 50), ('060', 60), ('080', 80), ('100', 100)]
CMAP = plt.cm.viridis


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


def hp(x, sr, fc=25.0):
    n = len(x)
    X = np.fft.rfft(x)
    f = np.fft.rfftfreq(n, 1.0 / sr)
    return np.fft.irfft(X * (1.0 / (1.0 + (fc / np.maximum(f, 1e-6)) ** 4)), n)


def seg(x, a, b, sr):
    return 20 * np.log10(max(np.sqrt(np.mean(x[int(a * sr):int(b * sr)] ** 2)), 1e-12))


dry, sr = read_wav(os.path.join(BASE, 'measure', 'signals', '02-release.wav'))
dry = hp(dry, sr)

TS = [2, 5, 8, 12, 18, 26, 36, 50, 70, 95, 130, 180, 240, 320, 430, 580, 780, 1050, 1400, 1800]


def release_curve(path, lat):
    wet, _ = read_wav(path)
    if lat > 0:
        wet = np.concatenate([wet[lat:], np.zeros(lat)])
    n = min(len(dry), len(wet))
    w = hp(wet[:n], sr)
    end = LEAD + 0.5
    pend = end + 8.0
    ref = seg(w, pend - 0.6, pend, sr)
    pts = []
    for ms in TS:
        at = end + ms * 1e-3
        if at + 0.025 > pend:
            break
        pts.append((ms, ref - seg(w, at, at + 0.025, sr)))
    return pts


def inst_tau(pts):
    """逐段反推瞬时时间常数：(τ, GR_mean)"""
    out = []
    for i in range(len(pts) - 1):
        (t0, g0), (t1, g1) = pts[i], pts[i + 1]
        if g0 <= 0.05 or g1 <= 0.02 or g1 >= g0:
            continue
        dt = t1 - t0
        tau = dt / np.log(g0 / g1)
        out.append((0.5 * (g0 + g1), tau))
    return out


fig, axes = plt.subplots(1, 3, figsize=(18, 5.5))

uad_all = []
our_all = []
print('%-6s | %-28s | %-28s' % ('Peak', 'UAD  τ·GR (按 GR 分箱)', '我们 τ·GR'))
print('-' * 90)

for idx, (tag, pk) in enumerate(PEAKS):
    up = os.path.join(REN, 'UAD-p%s.wav' % tag)
    op = os.path.join(REN, 'OURS-p%s.wav' % tag)
    if not (os.path.exists(up) and os.path.exists(op)):
        continue
    color = CMAP(idx / max(len(PEAKS) - 1, 1))
    ut = inst_tau(release_curve(up, 87))
    ot = inst_tau(release_curve(op, 0))
    if not ut or not ot:
        continue
    uad_all += [(g, t, pk) for g, t in ut]
    our_all += [(g, t, pk) for g, t in ot]

    axes[0].plot([g for g, _ in ut], [t for _, t in ut], '-o', ms=3, lw=1.4,
                 color=color, alpha=.9, label='Peak %d' % pk)
    axes[1].plot([g for g, _ in ut], [g * t for g, t in ut], '-o', ms=3, lw=1.4,
                 color=color, alpha=.9, label='Peak %d' % pk)

axes[2].scatter([g for g, _, _ in our_all], [t for _, t, _ in our_all],
                s=16, c='#f39c12', alpha=.75, label='TOiRE-2A')
axes[2].scatter([g for g, _, _ in uad_all], [t for _, t, _ in uad_all],
                s=16, c='#c0392b', alpha=.75, label='UAD LA-2A')

# 理论线：τ = A/GR
gs = np.linspace(0.3, 25, 200)
for A, c in [(1450, '#2980b9'), (2400, '#27ae60')]:
    axes[0].plot(gs, A / gs, '--', color=c, lw=1.2, alpha=.8, label='τ=A/GR  A=%d' % A)
    axes[2].plot(gs, A / gs, '--', color=c, lw=1.2, alpha=.6)
axes[1].axhline(1450, ls='--', color='#2980b9', lw=1.2, alpha=.8, label='A = 1450')
axes[1].axhline(2400, ls='--', color='#27ae60', lw=1.2, alpha=.8, label='A = 2400')
axes[2].plot(gs, 1450 / gs, '--', color='#2980b9', lw=1.2, alpha=.5)
axes[2].plot(gs, 2400 / gs, '--', color='#27ae60', lw=1.2, alpha=.5)

axes[0].set_xlabel('当前 GR (dB)')
axes[0].set_ylabel('瞬时时间常数 τ (ms)')
axes[0].set_title('UAD —— τ vs GR')
axes[0].set_xscale('log')
axes[0].set_yscale('log')
axes[0].set_ylim(20, 4000)
axes[0].grid(alpha=.3, which='both')
axes[0].legend(fontsize=6, ncol=2)

axes[1].set_xlabel('当前 GR (dB)')
axes[1].set_ylabel('τ · GR')
axes[1].set_title('UAD —— τ·GR vs GR（若为 1/GR 律应为水平线）')
axes[1].set_xscale('log')
axes[1].set_ylim(0, 4000)
axes[1].grid(alpha=.3, which='both')
axes[1].legend(fontsize=7)

axes[2].set_xlabel('当前 GR (dB)')
axes[2].set_ylabel('瞬时时间常数 τ (ms)')
axes[2].set_title('两家叠加对比')
axes[2].set_xscale('log')
axes[2].set_yscale('log')
axes[2].set_ylim(20, 4000)
axes[2].grid(alpha=.3, which='both')
axes[2].legend(fontsize=8)

fig.tight_layout()
out = os.path.join(REP, 'tau-vs-gr.png')
fig.savefig(out, dpi=140)
plt.close(fig)
print('\n输出:', out)

# ── 数值汇总 ────────────────────────────────────────────
print('\n=== UAD 的 τ·GR 按 GR 分箱 ===')
bins = [(0.3, 1), (1, 2), (2, 4), (4, 7), (7, 11), (11, 16), (16, 22), (22, 35)]
print('%12s %10s %10s %8s' % ('GR 区间', 'A 中位数', 'A 均值', '样本数'))
for lo, hi in bins:
    vals = [g * t for g, t, _ in uad_all if lo <= g < hi]
    if len(vals) < 3:
        continue
    print('%6.1f–%-5.1f %10.0f %10.0f %8d' %
          (lo, hi, np.median(vals), np.mean(vals), len(vals)))

print('\n=== 我们的 τ·GR 按 GR 分箱 ===')
for lo, hi in bins:
    vals = [g * t for g, t, _ in our_all if lo <= g < hi]
    if len(vals) < 3:
        continue
    print('%6.1f–%-5.1f %10.0f %10.0f %8d' %
          (lo, hi, np.median(vals), np.mean(vals), len(vals)))
