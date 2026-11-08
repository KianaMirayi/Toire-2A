# -*- coding: utf-8 -*-
"""
拟合框架 · 第一层：GR 曲线的静态形状

拟合对象：kKneeDb / kSlope / kSoftDb / kKnee2Db / kSlope2 / kSoft2Db
          + 8 点 Peak 映射表

目标数据：UAD 实测 GR 矩阵（11 个电平 × 7 档 Peak，来自 08-sat-sweep）

模型（稳态求解）：
    over  = L - GR + G - knee            （L=输入电平, G=侧链增益, GR=闭环压缩量）
    GR    = slope*SP(over, soft) + slope2*SP(over - knee2, soft2)
    其中 SP(x,k) = k*ln(1+exp(x/k))，再减去零点值使 GR 在 over<=0 时趋近 0

闭环 GR 用不动点迭代求解（这就是反馈拓扑的本质）。
"""
import os
import numpy as np
from scipy.optimize import least_squares

BASE = r'E:\Programing\C++Hell\TOiRE-2A\measure'
REN = os.path.join(BASE, 'renders', 'matrix')
SR = 48000
LEAD, SEG = 0.5, 1.5
LEVELS = list(range(-42, 1, 3))
PEAKS = [('000', 0), ('010', 10), ('020', 20), ('040', 40),
         ('050', 50), ('060', 60), ('080', 80)]
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


def seg(x, a, b):
    return 20 * np.log10(max(np.sqrt(np.mean(x[int(a * SR):int(b * SR)] ** 2)), 1e-12))


def level_curve(path, lat):
    x, _ = read_wav(path)
    if lat > 0:
        x = np.concatenate([x[lat:], np.zeros(lat)])
    return np.array([seg(x, LEAD + i * SEG + 0.3, LEAD + (i + 1) * SEG - 0.05)
                     for i in range(len(LEVELS))])


# ── 采集目标数据（纯压缩量 = PeakMin 渲染 − 当前 Peak 渲染）────────
print('读取 UAD 实测矩阵 …')
uad_base = level_curve(os.path.join(REN, 'UAD-pMIN.wav'), LAT)
TARGET = {}
for tag, pk in PEAKS:
    cur = level_curve(os.path.join(REN, 'UAD-p%s.wav' % tag), LAT)
    TARGET[pk] = uad_base - cur

mask = np.array([L >= -24 for L in LEVELS])          # 太轻的电平 SNR 不够
LV = np.array(LEVELS, float)


# ── 模型 ────────────────────────────────────────────────
def sp(x, k):
    return k * np.log1p(np.exp(np.clip(x / k, -50, 50)))


def gr_of_over(over, slope, soft, slope2, knee2, soft2):
    g = slope * (sp(over, soft) - sp(0.0, soft))
    g = g + slope2 * (sp(over - knee2, soft2) - sp(-knee2, soft2))
    return np.maximum(g, 0.0)


def solve_gr(L, G, knee, slope, soft, slope2, knee2, soft2):
    """闭环不动点：GR 使 over = L - GR + G - knee 自洽"""
    gr = 0.0
    for _ in range(60):
        over = L - gr + G - knee
        new = gr_of_over(over, slope, soft, slope2, knee2, soft2)
        if abs(new - gr) < 1e-6:
            gr = new
            break
        gr = 0.5 * gr + 0.5 * new
    return gr


# ── 参数向量 ────────────────────────────────────────────
# p = [knee, slope, soft, slope2, knee2, soft2, G0, G10, G20, G40, G50, G60, G80]
PEAK_IDX = {pk: i for i, (_, pk) in enumerate(PEAKS)}
NP = 6 + len(PEAKS)


def unpack(p):
    return (p[0], p[1], p[2], p[3], p[4], p[5]), {pk: p[6 + i] for i, (_, pk) in enumerate(PEAKS)}


def residuals(p):
    curve, gmap = unpack(p)
    knee, slope, soft, slope2, knee2, soft2 = curve
    out = []
    for tag, pk in PEAKS:
        for i, L in enumerate(LV):
            if not mask[i]:
                continue
            model = solve_gr(L, gmap[pk], knee, slope, soft, slope2, knee2, soft2)
            out.append(model - TARGET[pk][i])
    return np.array(out)


p0 = np.array([
    -7.37, 4.5, 2.0, 2.0, 10.0, 4.0,      # 曲线
    -2.4, -1.2, -0.5, 10.5, 15.3, 20.1, 34.6,   # Peak 映射
])

# 参数边界（避免跑到无意义的解）
lo = np.array([-20, 1.0, 0.3, 0.0, 1.0, 0.5, -30, -30, -30, -10, 0, 5, 20])
hi = np.array([5, 12.0, 8.0, 12.0, 30.0, 15.0, 10, 10, 10, 25, 30, 40, 50])

print('开始拟合（参数 %d 个，残差 %d 个）…' % (NP, len(residuals(p0))))
res = least_squares(residuals, p0, bounds=(lo, hi), method='trf',
                    x_scale='jac', max_nfev=4000, verbose=1)

p = res.x
curve, gmap = unpack(p)
knee, slope, soft, slope2, knee2, soft2 = curve
print('\n=== 拟合结果 ===')
print('  kKneeDb  = %.3f' % knee)
print('  kSlope   = %.3f' % slope)
print('  kSoftDb  = %.3f' % soft)
print('  kSlope2  = %.3f' % slope2)
print('  kKnee2Db = %.3f' % knee2)
print('  kSoft2Db = %.3f' % soft2)
print('  Peak 映射表:')
for tag, pk in PEAKS:
    print('    %3d → %+.2f dB' % (pk, gmap[pk]))
print('  残差 RMS = %.3f dB   最大 = %.3f dB' %
      (np.sqrt(np.mean(res.fun ** 2)), np.max(np.abs(res.fun))))

# ── 拟合后对照表 ────────────────────────────────────────
print('\n=== 拟合后对照（UAD 实测 / 模型 / 差）===')
hdr = '%8s' % '输入'
for _, pk in PEAKS:
    hdr += '%18s' % ('P%d' % pk)
print(hdr)
for i, L in enumerate(LV):
    if not mask[i]:
        continue
    line = '%8d' % L
    for tag, pk in PEAKS:
        m = solve_gr(L, gmap[pk], knee, slope, soft, slope2, knee2, soft2)
        t = TARGET[pk][i]
        line += '%18s' % ('%.2f/%.2f' % (t, m))
    print(line)

print('\n=== 残差热力图（模型 − UAD）===')
hdr = '%8s' % '输入'
for _, pk in PEAKS:
    hdr += '%8s' % ('P%d' % pk)
print(hdr)
for i, L in enumerate(LV):
    if not mask[i]:
        continue
    line = '%8d' % L
    for tag, pk in PEAKS:
        m = solve_gr(L, gmap[pk], knee, slope, soft, slope2, knee2, soft2)
        line += '%+8.2f' % (m - TARGET[pk][i])
    print(line)

# 保存给 C++ 用
out = os.path.join(BASE, '..', 'docs', 'reports', 'fit-gr-params.txt')
os.makedirs(os.path.dirname(out), exist_ok=True)
with open(out, 'w', encoding='utf-8') as f:
    f.write('# 由 fit_gr_curve.py 自动生成\n')
    f.write('kKneeDb  = %.4ff\n' % knee)
    f.write('kSlope   = %.4ff\n' % slope)
    f.write('kSoftDb  = %.4ff\n' % soft)
    f.write('kSlope2  = %.4ff\n' % slope2)
    f.write('kKnee2Db = %.4ff\n' % knee2)
    f.write('kSoft2Db = %.4ff\n' % soft2)
    f.write('# Peak 映射\n')
    for tag, pk in PEAKS:
        f.write('{ %.1ff, %.3ff },\n' % (pk, gmap[pk]))
    f.write('# 残差 RMS %.3f dB, 最大 %.3f dB\n' %
            (np.sqrt(np.mean(res.fun ** 2)), np.max(np.abs(res.fun))))
print('\n参数已写入:', os.path.abspath(out))
