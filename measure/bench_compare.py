# -*- coding: utf-8 -*-
"""
四插件基准测试（修正版）—— 把「饱和损失」从「压缩量」里分离出来

问题：之前用固定 makeup 反推 GR，把饱和吃掉的电平也算进了压缩量。
      加了饱和之后，我们的 GR 数字虚高 1.5 dB。

修正：
  gain_total(t) = wet_Peak50(t) − dry(t)      ← 压缩 + 饱和
  gain_sat(t)   = wet_PeakMin(t) − dry(t)     ← 只有饱和（无压缩）
  GR_true(t)    = gain_sat(t) − gain_total(t) ← 纯压缩量

输出：类别聚合表 + 柱状图 + 偏差图
"""
import os
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

matplotlib.rcParams['font.sans-serif'] = ['Microsoft YaHei', 'SimHei', 'DejaVu Sans']
matplotlib.rcParams['axes.unicode_minus'] = False

BASE = r'E:\Programing\C++Hell\TOiRE-2A'
EXC = os.path.join(BASE, 'measure', 'signals', 'bench', 'excerpt')
REN = os.path.join(BASE, 'measure', 'renders', 'bench')
REP = os.path.join(BASE, 'docs', 'reports')
os.makedirs(REP, exist_ok=True)

SR = 48000
WIN_MS = 50

PLUGINS = [
    ('UAD',  'UAD LA-2A',   87, '#c0392b', 'UADMIN--'),
    ('RF',   'RF-2A',        0, '#2980b9', None),        # RF-2A 无饱和级，不做修正
    ('REV',  'Revenger 2A',  0, '#27ae60', None),
    ('Ours', 'TOiRE-2A',     0, '#f39c12', 'OURSMIN--'),
]

CATS = {
    'Drums':  ['Record---Drums', 'emo-drums', 'fv-kick', 'as-kick',
               'as-kickout', 'as-overhead', 'eh-kick'],
    'Snare':  ['snare-top', 'eh-snare', 'fv-snare', 'as-snarebot', 'eh-snarebot'],
    'Bass':   ['Record---Bass', 'eh-bass', 'fv-bass', 'eh-bassmic'],
    'Vocal':  ['Record---Vocal', 'emo-vocals', 'vocal-lead', 'eh-vocal',
               'fv-vocal', 'as-bv1'],
    'Guitar': ['Record---Guitar', 'as-guitar', 'eh-guitar', 'fv-guitar',
               'as-amp2', 'eh-ignater'],
    'Keys':   ['keys-synth', 'as-wurli', 'fv-keys', 'as-organ', 'eh-keys'],
}


def read_wav(path):
    with open(path, 'rb') as f:
        if f.read(4) != b'RIFF':
            raise ValueError('not RIFF: ' + path)
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


def hp(x, sr, fc=25.0):
    n = len(x)
    X = np.fft.rfft(x)
    f = np.fft.rfftfreq(n, 1.0 / sr)
    return np.fft.irfft(X * (1.0 / (1.0 + (fc / np.maximum(f, 1e-6)) ** 4)), n)


def wr(x, sr, ms=WIN_MS):
    n = int(sr * ms / 1000)
    m = len(x) // n * n
    return np.sqrt(np.mean(x[:m].reshape(-1, n) ** 2, axis=1))


per_mat = {}
print('%-18s %-8s' % ('素材', '类别') + ''.join('%14s' % p[1] for p in PLUGINS))
print('-' * (26 + 14 * len(PLUGINS)))

for cat, stems in CATS.items():
    for stem in stems:
        p = os.path.join(EXC, stem + '.wav')
        if not os.path.exists(p):
            continue
        dry, sr = read_wav(p)
        dry = hp(dry, sr)
        dd = 20 * np.log10(np.maximum(wr(dry, sr), 1e-9))
        vals = {}
        for key, name, lat, _, minprefix in PLUGINS:
            fp = os.path.join(REN, '%s--%s.wav' % (key, stem))
            if not os.path.exists(fp):
                continue
            wet, _ = read_wav(fp)
            if lat > 0:
                wet = np.concatenate([wet[lat:], np.zeros(lat)])
            n = min(len(dry), len(wet))
            ww = 20 * np.log10(np.maximum(wr(hp(wet[:n], sr), sr), 1e-9))
            d2 = dd[:len(ww)]
            if minprefix is None:
                gr = (14.5 - (ww - d2))            # RF/REV：无饱和修正
            else:
                fp2 = os.path.join(REN, '%s%s.wav' % (minprefix, stem))
                if not os.path.exists(fp2):
                    gr = (14.5 - (ww - d2))
                else:
                    wmin, _ = read_wav(fp2)
                    if lat > 0:
                        wmin = np.concatenate([wmin[lat:], np.zeros(lat)])
                    mm = min(len(wmin), len(dry))
                    wm = 20 * np.log10(np.maximum(wr(hp(wmin[:mm], sr), sr), 1e-9))
                    k = min(len(wm), len(ww))
                    gr = wm[:k] - ww[:k]            # 纯压缩量 = 饱和损失 − 总损失
                    d2 = d2[:k]
            m = d2 > -60
            if m.sum() < 10:
                continue
            vals[key] = float(gr[m].mean())
            per_mat[(stem, key)] = vals[key]
        if len(vals) < len(PLUGINS):
            continue
        print('%-18s %-8s' % (stem[:18], cat) +
              ''.join('%14.2f' % vals[k] for k, *_ in PLUGINS))

cat_mean = {}
for cat, stems in CATS.items():
    row = []
    for key, *_ in PLUGINS:
        v = [per_mat[(s, key)] for s in stems if (s, key) in per_mat]
        row.append(float(np.mean(v)) if v else np.nan)
    cat_mean[cat] = row

print('\n=== 按类别平均（修正后：纯压缩量）===')
print('%-10s' % '类别' + ''.join('%14s' % p[1] for p in PLUGINS) + '%12s' % '我们−UAD')
print('-' * (10 + 14 * len(PLUGINS) + 12))
for cat, row in cat_mean.items():
    print('%-10s' % cat + ''.join('%14.2f' % v for v in row) + '%+11.2f' % (row[3] - row[0]))

fig, ax = plt.subplots(figsize=(12, 5.6))
x = np.arange(len(cat_mean))
wb = 0.2
for i, (key, name, lat, color, _) in enumerate(PLUGINS):
    ax.bar(x + (i - 1.5) * wb, [cat_mean[c][i] for c in cat_mean], wb,
           label=name, color=color, alpha=.9)
ax.set_xticks(x); ax.set_xticklabels(list(cat_mean.keys()))
ax.set_ylabel('纯压缩量 GR (dB)'); ax.grid(axis='y', alpha=.3); ax.legend()
ax.set_title('四插件 · 按类别平均压缩量（已分离饱和损失，Peak 50 / makeup +14.5 dB）')
fig.tight_layout(); fig.savefig(os.path.join(REP, 'bench-cat-bars.png'), dpi=140); plt.close(fig)

fig, ax = plt.subplots(figsize=(11, 5))
for i, (key, name, lat, color, _) in enumerate(PLUGINS):
    if key == 'UAD':
        continue
    d = [cat_mean[c][i] - cat_mean[c][0] for c in cat_mean]
    ax.bar(x + (i - 2) * 0.25, d, 0.25, label=name + ' − UAD', color=color, alpha=.9)
ax.axhline(0, color='#333', lw=1)
ax.set_xticks(x); ax.set_xticklabels(list(cat_mean.keys()))
ax.set_ylabel('压缩量差值 (dB)'); ax.grid(axis='y', alpha=.3); ax.legend()
ax.set_title('各插件相对 UAD 的压缩量偏差（修正后）')
fig.tight_layout(); fig.savefig(os.path.join(REP, 'bench-cat-diff.png'), dpi=140); plt.close(fig)

import csv
with open(os.path.join(REP, 'bench-cat.csv'), 'w', newline='', encoding='utf-8-sig') as f:
    cw = csv.writer(f)
    cw.writerow(['类别', '素材数'] + [p[1] for p in PLUGINS] + ['我们−UAD'])
    for cat, row in cat_mean.items():
        cw.writerow([cat, len(CATS[cat])] + [round(v, 2) for v in row] + [round(row[3] - row[0], 2)])

print('\n输出：docs/reports/bench-cat-bars.png / bench-cat-diff.png / bench-cat.csv')
