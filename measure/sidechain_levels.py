# -*- coding: utf-8 -*-
"""
侧链响应的「电平依赖性」检查

问题：同一个 H(f) 在不同输入电平下是否一致？
  一致  → 检测器是线性的，H(f) 可以直接当线性滤波器建模
  不一致 → 检测器非线性，建模要在检测器里体现电平依赖

方法：等 GR 法（见 sidechain_equalgr.py），对 4 个电平各算一遍 H(f)
"""
import os
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

matplotlib.rcParams['font.sans-serif'] = ['Microsoft YaHei', 'SimHei', 'DejaVu Sans']
matplotlib.rcParams['axes.unicode_minus'] = False

BASE = r'E:\Programing\C++Hell\TOiRE-2A'
SIG = os.path.join(BASE, 'measure', 'signals')
REN = os.path.join(BASE, 'measure', 'renders')
SC = os.path.join(REN, 'sc')
REP = os.path.join(BASE, 'docs', 'reports')

SR = 48000
ST_LEAD, ST_STEP = 0.5, 1.5
ST_LEVELS = list(range(-60, 1, 3))
FQ_LEAD, FQ_SEG, FQ_SETTLE = 0.5, 2.0, 1.2

FREQS = [20, 25, 31.5, 40, 50, 63, 80, 100, 125, 160,
         200, 250, 315, 400, 500, 630, 800, 1000, 1250, 1600,
         2000, 2500, 3150, 4000, 5000, 6300, 8000, 10000, 12500, 16000]

# (电平dBFS, 干信号, UAD 文件, RF 文件)
LEVELS = [
    (-6.0,  '05-freq-dense-m06.wav', 'L-UAD-m06.wav',      'L-RF-m06.wav'),
    (-12.0, '05-freq-dense.wav',     'D-UAD.wav',          'D-RF-r37_000.wav'),
    (-18.0, '05-freq-dense-m18.wav', 'L-UAD-m18.wav',      'L-RF-m18.wav'),
    (-24.0, '05-freq-dense-m24.wav', 'L-UAD-m24.wav',      'L-RF-m24.wav'),
]

STATIC = {
    'UAD':   (os.path.join(REN, 'R1-comp--01-io-stairs.wav'), 87),
    'RF-2A': (os.path.join(REN, 'RF--01-io-stairs.wav'),      0),
}
LAT = {'UAD': 87, 'RF-2A': 0}


def read_wav(path):
    with open(path, 'rb') as f:
        if f.read(4) != b'RIFF':
            raise ValueError('not RIFF: ' + path)
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


def hp(x, sr, fc=25.0):
    n = len(x)
    X = np.fft.rfft(x)
    f = np.fft.rfftfreq(n, 1.0 / sr)
    return np.fft.irfft(X * (1.0 / (1.0 + (fc / np.maximum(f, 1e-6)) ** 4)), n)


def seg_db(x, t0, t1, sr):
    s, e = int(t0 * sr), int(t1 * sr)
    return 20 * np.log10(max(np.sqrt(np.mean(x[s:e] ** 2)), 1e-12))


def shift(x, lat):
    return np.concatenate([x[lat:], np.zeros(lat)]) if lat > 0 else x


def static_curve(path, lat):
    dry, sr = read_wav(os.path.join(SIG, '01-io-stairs.wav'))
    wet, _ = read_wav(path)
    wet = shift(wet, lat)
    n = min(len(dry), len(wet))
    dry, wet = hp(dry[:n], sr), hp(wet[:n], sr)
    gc = []
    for i in range(len(ST_LEVELS)):
        a = ST_LEAD + i * ST_STEP
        gc.append(seg_db(wet, a + 0.2, a + ST_STEP, sr) - seg_db(dry, a + 0.2, a + ST_STEP, sr))
    gc = np.array(gc)
    o = np.argsort(gc)
    return gc[o], np.array(ST_LEVELS)[o]


def h_curve(freq_path, dry_path, lat, test_level, gc_s, lv_s):
    """dry_path 必须是和该电平对应的干信号（曾经写死成 -12 那个，导致整体偏移）"""
    dry, sr = read_wav(os.path.join(SIG, dry_path))
    wet, _ = read_wav(freq_path)
    wet = shift(wet, lat)
    n = min(len(dry), len(wet))
    dry, wet = hp(dry[:n], sr), hp(wet[:n], sr)
    h, valid = [], []
    for i in range(len(FREQS)):
        a = FQ_LEAD + i * FQ_SEG
        g = seg_db(wet, a + FQ_SEG - FQ_SETTLE, a + FQ_SEG, sr) - \
            seg_db(dry, a + FQ_SEG - FQ_SETTLE, a + FQ_SEG, sr)
        # GR = makeup - g；只有真正在压缩时才反查得出东西
        gr = 14.5 - g
        if gr < 1.0 or g > gc_s.max() - 0.05 or g < gc_s.min() + 0.05:
            h.append(np.nan)
            valid.append(False)
        else:
            h.append(np.interp(g, gc_s, lv_s) - test_level)
            valid.append(True)
    return np.array(h), np.array(valid)


fig, axes = plt.subplots(1, 2, figsize=(15, 6.2))
colors = ['#c0392b', '#e67e22', '#2980b9', '#8e44ad']

for pi, plugin in enumerate(['UAD', 'RF-2A']):
    ax = axes[pi]
    spath, slat = STATIC[plugin]
    gc_s, lv_s = static_curve(spath, slat)
    curves = []
    print('\n=== %s ===' % plugin)
    print('%-8s' % '电平' + ''.join('%8s' % ('%g' % f) for f in [50, 100, 500, 1000, 4000, 10000, 16000]))
    for li, (lev, dryf, uf, rf_file) in enumerate(LEVELS):
        fn = os.path.join(SC, uf if plugin == 'UAD' else rf_file)
        if not os.path.exists(fn):
            print('  缺少', os.path.basename(fn))
            continue
        h, valid = h_curve(fn, dryf, LAT[plugin], lev, gc_s, lv_s)
        curves.append(h)
        ax.plot(FREQS, h, 'o-', color=colors[li], ms=3, lw=1.4, alpha=.85,
                label='%g dBFS  (%d/%d 有效)' % (lev, valid.sum(), len(valid)))
        print('%-8g' % lev + ''.join('%8.1f' % h[FREQS.index(f)] for f in [50, 100, 500, 1000, 4000, 10000, 16000])
              + '   [%d/%d 有效]' % (valid.sum(), len(valid)))

    if len(curves) >= 2:
        C = np.array(curves)
        with np.errstate(invalid='ignore'):
            spread = np.nanmax(C, axis=0) - np.nanmin(C, axis=0)
        ok = np.isfinite(spread)
        if ok.any():
            print('  电平间最大偏差: %.1f dB  (中位 %.1f dB, 有效频点 %d)'
                  % (np.nanmax(spread), np.nanmedian(spread), ok.sum()))
        ax.fill_between(FREQS, np.nanmin(C, axis=0), np.nanmax(C, axis=0),
                        color='#999', alpha=.15, label='电平间范围')
    ax.set_xscale('log')
    ax.set_xticks([20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 16000])
    ax.set_xticklabels(['20', '50', '100', '200', '500', '1k', '2k', '5k', '10k', '16k'])
    ax.set_xlabel('频率 (Hz)')
    ax.set_ylabel('H(f)  (dB)')
    ax.set_title('%s — 不同电平下的侧链响应' % plugin)
    ax.grid(alpha=.3, which='both')
    ax.axhline(0, color='#999', lw=.8, ls='--')
    ax.legend(fontsize=8, loc='best')

fig.suptitle('侧链响应的电平依赖性（等 GR 法）— 曲线越重合 = 检测器越线性', fontsize=12)
fig.tight_layout()
fig.savefig(os.path.join(REP, 'sidechain-vs-level.png'), dpi=140)
plt.close(fig)
print('\n输出: docs/reports/sidechain-vs-level.png')
