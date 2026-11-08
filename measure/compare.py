# -*- coding: utf-8 -*-
"""
对比 UAD LA-2A 与 RF-2A 在真实素材上的表现

输入：measure/signals/real/*.wav (dry) + measure/renders/real/{UAD,RF}--*.wav
输出：docs/reports/real-compare.png + real-compare.csv + 终端统计
"""
import os, wave
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

matplotlib.rcParams['font.sans-serif'] = ['Microsoft YaHei', 'SimHei', 'DejaVu Sans']
matplotlib.rcParams['axes.unicode_minus'] = False

BASE = r'E:\Programing\C++Hell\TOiRE-2A'
SIG = os.path.join(BASE, 'measure', 'signals', 'real')
REN = os.path.join(BASE, 'measure', 'renders', 'real')
REP = os.path.join(BASE, 'docs', 'reports')
os.makedirs(REP, exist_ok=True)

LATENCY = {'UAD': 87, 'RF': 0}
WIN_MS = 50            # 分析窗口 50 ms

# makeup 用正弦阶梯测出来的固定值，不从素材里估
# （持续音素材从头压到尾，没有"未压缩"段落可以估）
MAKEUP = {'UAD': 14.50, 'RF': 4.58}

CASES = [('10-snare', 'Snare'), ('11-vocal', 'Lead Vocal'), ('12-keys', 'Keys/Synth')]


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
                tag, nch, sr = int.from_bytes(b[0:2], 'little'), int.from_bytes(b[2:4], 'little'), int.from_bytes(b[4:8], 'little')
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


def hp_filter(x, sr, fc=30.0):
    """FFT 高通（约 −24 dB/oct），去掉 UAD 建模产生的 <20 Hz 亚音速成分。
    实测：安静段 99.9% 能量集中在 2 Hz，会把 RMS 测量带偏 20+ dB。"""
    n = len(x)
    if n < 1024:
        return x
    X = np.fft.rfft(x)
    f = np.fft.rfftfreq(n, 1.0 / sr)
    g = 1.0 / (1.0 + (fc / np.maximum(f, 1e-6)) ** 4)
    return np.fft.irfft(X * g, n)


def win_rms(x, sr, ms=WIN_MS):
    n = int(sr * ms / 1000)
    m = len(x) // n * n
    return np.sqrt(np.mean(x[:m].reshape(-1, n) ** 2, axis=1)), sr / n


def align(x, lat, sr):
    if lat > 0:
        return np.concatenate([x[lat:], np.zeros(lat)])
    return x


fig, axes = plt.subplots(3, 2, figsize=(15, 11))

rows = []
print('%-14s %8s %8s %9s %9s %9s %9s' %
      ('素材', 'makeup_UAD', 'makeup_RF', 'GR均值UAD', 'GR均值RF', 'GR相关', '平均差'))
print('-' * 82)

for i, (stem, label) in enumerate(CASES):
    dry, sr = read_wav(os.path.join(SIG, stem + '.wav'))
    uad, _ = read_wav(os.path.join(REN, 'UAD--' + stem + '.wav'))
    rf, _ = read_wav(os.path.join(REN, 'RF--' + stem + '.wav'))

    dry = hp_filter(dry, sr)
    uad = hp_filter(uad, sr)
    rf  = hp_filter(rf, sr)
    dry = dry[:min(len(dry), len(uad), len(rf))]
    n = min(len(dry), len(uad) - LATENCY['UAD'])
    dry = dry[:n]
    uad = align(uad[:n + LATENCY['UAD']], LATENCY['UAD'], sr)[:n]
    rf = rf[:n]

    d, _ = win_rms(dry, sr)
    u, fps = win_rms(uad[:len(d) * int(sr * WIN_MS / 1000)], sr)
    r, _ = win_rms(rf[:len(d) * int(sr * WIN_MS / 1000)], sr)
    L = min(len(d), len(u), len(r))
    d, u, r = d[:L], u[:L], r[:L]

    with np.errstate(divide='ignore'):
        gd = 20 * np.log10(np.maximum(d, 1e-9))
        gu = 20 * np.log10(np.maximum(u, 1e-9))
        gr = 20 * np.log10(np.maximum(r, 1e-9))

    # 只在「有真实内容」且「GR 物理合理」的窗口比较
    # （排除噪声段和慢释放把残留能量抬出来的窗口，那些会让 GR 变成负数）
    GRu_all = MAKEUP['UAD'] - (gu - gd)
    GRr_all = MAKEUP['RF'] - (gr - gd)
    mask = (gd > -50) & (GRu_all >= -1.0) & (GRu_all <= 40) & (GRr_all >= -1.0) & (GRr_all <= 40)
    gain_u = (gu - gd)[mask]
    gain_r = (gr - gd)[mask]

    # 用固定 makeup（正弦阶梯实测值）
    mk_u = MAKEUP['UAD']
    mk_r = MAKEUP['RF']

    GRu = mk_u - gain_u
    GRr = mk_r - gain_r

    corr = float(np.corrcoef(GRu, GRr)[0, 1]) if len(GRu) > 10 else float('nan')
    mae = float(np.mean(np.abs(GRu - GRr)))
    rows.append([label, mk_u, mk_r, float(np.mean(GRu)), float(np.mean(GRr)), corr, mae])
    print('%-14s %8.2f %8.2f %9.2f %9.2f %9.3f %9.2f' %
          (label, mk_u, mk_r, np.mean(GRu), np.mean(GRr), corr, mae))

    t = np.arange(L) * (WIN_MS / 1000.0)
    tm = t[mask]

    ax = axes[i][0]
    ax.plot(t, gu - gd, lw=.6, color='#c0392b', alpha=.85, label='UAD')
    ax.plot(t, gr - gd, lw=.6, color='#2980b9', alpha=.85, label='RF-2A')
    ax.set_title('%s  —  增益变化 (dB)' % label)
    ax.set_ylabel('dB'); ax.grid(alpha=.25); ax.legend(loc='lower right', fontsize=8)
    ax.set_ylim(-40, 25)

    ax = axes[i][1]
    ax.plot(tm, GRu, lw=.6, color='#c0392b', alpha=.85, label='UAD')
    ax.plot(tm, GRr, lw=.6, color='#2980b9', alpha=.85, label='RF-2A')
    ax.set_title('%s  —  GR  (r=%.3f, MAE=%.2f dB)' % (label, corr, mae))
    ax.set_ylabel('GR (dB)'); ax.grid(alpha=.25); ax.legend(loc='upper right', fontsize=8)
    ax.set_ylim(-5, 40)

axes[-1][0].set_xlabel('时间 (s)')
axes[-1][1].set_xlabel('时间 (s)')
fig.tight_layout()
fig.savefig(os.path.join(REP, 'real-compare.png'), dpi=130)
plt.close(fig)

import csv
with open(os.path.join(REP, 'real-compare.csv'), 'w', newline='', encoding='utf-8-sig') as f:
    cw = csv.writer(f)
    cw.writerow(['素材', 'makeup_UAD_dB', 'makeup_RF_dB', 'GR均值_UAD', 'GR均值_RF', 'GR相关', '平均差_dB'])
    for r in rows:
        cw.writerow([r[0]] + [round(v, 3) for v in r[1:]])

print('\n输出: docs/reports/real-compare.png / .csv')
