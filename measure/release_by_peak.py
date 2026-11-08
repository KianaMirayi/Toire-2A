# -*- coding: utf-8 -*-
"""
Peak 全扫释放测量：UAD vs TOiRE-2A

对每个 Peak 位置测：
  起始 GR（500 ms 过载结束时的压缩量）
  t50 / t80（恢复到起始值的 50% / 80% 所需时间）
  释放曲线（1..1600 ms）

目的：定位"浅压缩（Peak 0-40）时回声"的差异
"""
import os
import numpy as np

BASE = r'E:\Programing\C++Hell\TOiRE-2A'
REN = os.path.join(BASE, 'measure', 'renders', 'relpeak')
SR = 48000
LEAD = 0.5
CASES = [(0.5, 8.0), (0.02, 8.0), (2.0, 10.0)]
PEAKS = [('000', 0), ('010', 10), ('020', 20), ('030', 30), ('040', 40),
         ('050', 50), ('060', 60), ('080', 80), ('100', 100)]


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


def analyse(path, lat):
    wet, _ = read_wav(path)
    if lat > 0:
        wet = np.concatenate([wet[lat:], np.zeros(lat)])
    n = min(len(dry), len(wet))
    d, w = hp(dry[:n], sr), hp(wet[:n], sr)
    t = LEAD
    out = []
    for burst, probe in CASES:
        end = t + burst
        pend = end + probe
        ref = seg(w, pend - 0.6, pend, sr)
        g0 = ref - seg(w, end + 0.002, end + 0.012, sr)
        pts = []
        for ms in [1, 5, 10, 20, 40, 60, 100, 150, 200, 300, 400, 600, 800, 1200, 1600]:
            at = end + ms * 1e-3
            if at + 0.03 > pend:
                break
            pts.append((ms, ref - seg(w, at, at + 0.03, sr)))

        def tfrac(frac):
            tgt = g0 * frac
            for i in range(1, len(pts)):
                if pts[i][1] <= tgt:
                    (m0, a0), (m1, a1) = pts[i - 1], pts[i]
                    return m0 + (m1 - m0) * (a0 - tgt) / max(a0 - a1, 1e-9)
            return None

        out.append(dict(burst=burst, g0=g0, t50=tfrac(0.5), t80=tfrac(0.2), pts=pts))
        t = pend
    return out


print('=' * 96)
print('500 ms 过载 —— 各 Peak 下的压缩量与恢复时间')
print('=' * 96)
print('%6s | %8s %6s %6s | %8s %6s %6s | %7s %7s' %
      ('Peak', 'UAD GR', 't50', 't80', '我们GR', 't50', 't80', 'ΔGR', 't50比'))
print('-' * 96)
rows = []
for tag, pk in PEAKS:
    up = os.path.join(REN, 'UAD-p%s.wav' % tag)
    op = os.path.join(REN, 'OURS-p%s.wav' % tag)
    if not (os.path.exists(up) and os.path.exists(op)):
        continue
    u = analyse(up, 87)[0]
    o = analyse(op, 0)[0]
    r = (o['t50'] / u['t50']) if (u['t50'] and o['t50']) else float('nan')
    rows.append((pk, u, o))
    print('%6d | %8.2f %6.0f %6.0f | %8.2f %6.0f %6.0f | %+7.2f %7.2f' %
          (pk, u['g0'], u['t50'] or 0, u['t80'] or 0,
           o['g0'], o['t50'] or 0, o['t80'] or 0, o['g0'] - u['g0'], r))

print('\n' + '=' * 96)
print('完整释放曲线（500 ms 过载）')
print('=' * 96)
COMMON = [1, 5, 10, 20, 40, 60, 100, 150, 200, 300, 400, 600, 800, 1200, 1600]
for pk, u, o in rows:
    umap = dict(u['pts'])
    omap = dict(o['pts'])
    print('\nPeak %d:' % pk)
    print('  %8s' % 't(ms)' + ''.join('%8d' % m for m in COMMON if m in umap or m in omap))
    print('  %8s' % 'UAD' + ''.join('%8.2f' % umap[m] for m in COMMON if m in umap))
    print('  %8s' % '我们' + ''.join('%8.2f' % omap[m] for m in COMMON if m in omap))
    print('  %8s' % '差' +
          ''.join('%+8.2f' % (omap[m] - umap[m]) for m in COMMON if m in umap and m in omap))
