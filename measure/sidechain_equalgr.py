# -*- coding: utf-8 -*-
"""
等 GR 法测量侧链频率响应（消除 GR 饱和的影响）

原理
────
侧链上的滤波器等效于给检测器加了一个"电平偏移" H(f)：
    侧链看到的电平 = 输入电平 + H(f)

所以频率 f、电平 L 处的增益变化，等于 1 kHz、电平 L+H(f) 处的增益变化：
    g_f(L) = g_1k(L + H(f))

反解：先测出 g_1k(·) 这条静态曲线，再对每个频率找出
      「哪个电平能让 1 kHz 产生同样的增益变化」
      那个电平减去测试电平，就是 H(f)

这样完全绕开了 GR 饱和 —— 因为不再需要把 GR 差当作侧链响应。

输入
────
  静态曲线：renders/R1-comp--01-io-stairs.wav        (UAD, Peak 50)
            renders/RF--01-io-stairs.wav             (RF-2A, R37=0)
  频率扫描：renders/sc/D-UAD.wav
            renders/sc/D-RF-r37_000/050/100.wav
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
os.makedirs(REP, exist_ok=True)

SR = 48000

# 静态曲线布局
ST_LEAD, ST_STEP = 0.5, 1.5
ST_LEVELS = list(range(-60, 1, 3))

# 频率扫描布局
FQ_LEAD, FQ_SEG, FQ_SETTLE, FQ_LEVEL = 0.5, 2.0, 1.2, -12.0

FREQS = [20, 25, 31.5, 40, 50, 63, 80, 100, 125, 160,
         200, 250, 315, 400, 500, 630, 800, 1000, 1250, 1600,
         2000, 2500, 3150, 4000, 5000, 6300, 8000, 10000, 12500, 16000]

CASES = [
    ('D-UAD.wav',        'UAD',              87, os.path.join(REN, 'R1-comp--01-io-stairs.wav'), 87),
    ('D-RF-r37_000.wav', 'RF-2A  R37 = 0%',   0, os.path.join(REN, 'RF--01-io-stairs.wav'),      0),
    ('D-RF-r37_050.wav', 'RF-2A  R37 = 50%',  0, os.path.join(REN, 'RF--01-io-stairs.wav'),      0),
    ('D-RF-r37_100.wav', 'RF-2A  R37 = 100%', 0, os.path.join(REN, 'RF--01-io-stairs.wav'),      0),
]


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
    y = x[s:e]
    return 20 * np.log10(max(np.sqrt(np.mean(y ** 2)), 1e-12))


def shift(x, lat):
    if lat > 0:
        return np.concatenate([x[lat:], np.zeros(lat)])
    return x


def build_static(static_path, lat, sr_ref):
    """返回 (levels, gainChange) 静态曲线"""
    dry, sr = read_wav(os.path.join(SIG, '01-io-stairs.wav'))
    wet, _ = read_wav(static_path)
    wet = shift(wet, lat)
    n = min(len(dry), len(wet))
    dry, wet = dry[:n], wet[:n]
    dry, wet = hp(dry, sr), hp(wet, sr)
    lv, gc = [], []
    for i, L in enumerate(ST_LEVELS):
        a = ST_LEAD + i * ST_STEP
        b = a + ST_STEP
        d = seg_db(dry, a + 0.2, b, sr)
        w = seg_db(wet, a + 0.2, b, sr)
        lv.append(L)
        gc.append(w - d)
    return np.array(lv), np.array(gc)


def measure_freq(freq_path, lat):
    dry, sr = read_wav(os.path.join(SIG, '05-freq-dense.wav'))
    wet, _ = read_wav(freq_path)
    wet = shift(wet, lat)
    n = min(len(dry), len(wet))
    dry, wet = dry[:n], wet[:n]
    dry, wet = hp(dry, sr), hp(wet, sr)
    out = []
    for i, f in enumerate(FREQS):
        a = FQ_LEAD + i * FQ_SEG
        b = a + FQ_SEG
        d = seg_db(dry, a + FQ_SEG - FQ_SETTLE, b, sr)
        w = seg_db(wet, a + FQ_SEG - FQ_SETTLE, b, sr)
        out.append(w - d)
    return np.array(out)


fig, ax = plt.subplots(figsize=(11, 7))
colors = ['#c0392b', '#2980b9', '#27ae60', '#8e44ad']
results = {}

print('等 GR 法：把每个频率的增益变化，换算成"等效 1 kHz 电平偏移"\n')
for ci, (fname, label, lat, static_path, st_lat) in enumerate(CASES):
    fp = os.path.join(SC, fname)
    if not os.path.exists(fp):
        print('  缺少', fname)
        continue
    lv, gc = build_static(static_path, st_lat, SR)
    gcf = measure_freq(fp, lat)

    # 单调化后插值：给定 gainChange，求电平
    order = np.argsort(gc)
    gc_s, lv_s = gc[order], lv[order]

    h = []
    clamped = 0
    for f, g in zip(FREQS, gcf):
        if g > gc_s.max() or g < gc_s.min():
            clamped += 1
            g = np.clip(g, gc_s.min() + 1e-3, gc_s.max() - 1e-3)
        L_equiv = np.interp(g, gc_s, lv_s)
        h.append(L_equiv - FQ_LEVEL)
    h = np.array(h)
    results[label] = (np.array(FREQS), h)

    print('%-18s  1 kHz 处 H = %+5.2f dB   （参考点，应为 0）' % (label, h[FREQS.index(1000)]))
    if clamped:
        print('      ⚠ %d 个频率超出静态曲线范围，已钳位' % clamped)
    print('      ' + ' '.join('%s:%+.1f' % (('%g' % f), v) for f, v in zip(FREQS, h) if f in (50, 100, 500, 1000, 4000, 10000, 16000)))

    ax.plot(FREQS, h, 'o-', color=colors[ci % len(colors)], ms=3, lw=1.5, label=label)

ax.set_xscale('log')
ax.set_xticks([20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 16000])
ax.set_xticklabels(['20', '50', '100', '200', '500', '1k', '2k', '5k', '10k', '16k'])
ax.set_xlabel('频率 (Hz)')
ax.set_ylabel('侧链等效电平偏移 H(f)  (dB)')
ax.set_title('侧链频率响应 —— 等 GR 法（已消除 GR 饱和）\n'
             '正值 = 侧链对该频率更敏感（相当于把信号放大了这么多 dB）')
ax.grid(alpha=.3, which='both')
ax.axhline(0, color='#999', lw=.9, ls='--')
ax.axvline(1000, color='#ccc', lw=.8, ls=':')
ax.legend(fontsize=9)
fig.tight_layout()
fig.savefig(os.path.join(REP, 'sidechain-equalgr.png'), dpi=140)
plt.close(fig)

# CSV
import csv
with open(os.path.join(REP, 'sidechain-equalgr.csv'), 'w', newline='', encoding='utf-8-sig') as f:
    cw = csv.writer(f)
    cw.writerow(['频率Hz'] + [l for l, _ in results.items()])
    for i, f in enumerate(FREQS):
        cw.writerow([f] + [round(results[l][1][i], 2) for l in results])

print('\n输出: docs/reports/sidechain-equalgr.png / .csv')
