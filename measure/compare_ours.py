# -*- coding: utf-8 -*-
"""
TOiRE-2A（我们自己）vs UAD LA-2A —— 同一组测试信号的三项对比

1. 静态 IO 曲线
2. Attack（硬起音后 GR 的建立）
3. Release（过载后 GR 的恢复）

参数对齐：Peak Reduction 拨盘 50；makeup 两边都是 +14.5 dB
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
OURS = os.path.join(REN, 'ours')
REP = os.path.join(BASE, 'docs', 'reports')

SR = 48000
MAKEUP = {'UAD': 14.50, 'TOiRE': 14.50}

IO_LEAD, IO_STEP, IO_LEVELS = 0.5, 1.5, list(range(-60, 1, 3))
ATK_LEAD, ATK_HOLD, ATK_GAP, ATK_LEVELS = 0.8, 2.0, 0.8, [-6.0, -12.0, -20.0]
REL_LEAD, REL_CASES = 0.5, [(0.5, 8.0), (0.02, 8.0), (2.0, 10.0)]

PLUGINS = {
    'UAD':   dict(io=os.path.join(REN, 'R1-comp--01-io-stairs.wav'),
                  atk=os.path.join(REN, 'R1-comp--03-attack.wav'),
                  rel=os.path.join(REN, 'R1-comp--02-release.wav'),
                  lat=87, color='#c0392b'),
    'TOiRE': dict(io=os.path.join(OURS, 'ours--01-io-stairs.wav'),
                  atk=os.path.join(OURS, 'ours--03-attack.wav'),
                  rel=os.path.join(OURS, 'ours--02-release.wav'),
                  lat=0, color='#2980b9'),
}


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


def load(dry_path, wet_path, lat):
    dry, sr = read_wav(dry_path)
    wet, _ = read_wav(wet_path)
    if lat > 0:
        wet = np.concatenate([wet[lat:], np.zeros(lat)])
    n = min(len(dry), len(wet))
    return hp(dry[:n], sr), hp(wet[:n], sr), sr


def seg_db(x, t0, t1, sr):
    s, e = int(t0 * sr), int(t1 * sr)
    return 20 * np.log10(max(np.sqrt(np.mean(x[s:e] ** 2)), 1e-12))


fig, axes = plt.subplots(1, 3, figsize=(17, 5.4))

# ── 1. IO 曲线 ────────────────────────────────────────────
ax = axes[0]
print('=== 静态 IO 曲线（GR vs 输入）===')
print('%-8s' % '输入' + ''.join('%10s' % k for k in PLUGINS))
print('-' * 28)
io_curves = {}
for name, cfg in PLUGINS.items():
    dry, wet, sr = load(os.path.join(SIG, '01-io-stairs.wav'), cfg['io'], cfg['lat'])
    grs = []
    for i, L in enumerate(IO_LEVELS):
        a = IO_LEAD + i * IO_STEP
        b = a + IO_STEP
        gc = seg_db(wet, a + 0.2, b, sr) - seg_db(dry, a + 0.2, b, sr)
        grs.append(-(MAKEUP[name] - gc))
    grs = np.array(grs)
    io_curves[name] = grs
    ax.plot(IO_LEVELS, grs, 'o-', color=cfg['color'], ms=4, lw=1.7, label=name)
for i, L in enumerate(IO_LEVELS):
    if L % 6 == 0 or L in (-27, -12, 0):
        print('%-8d' % L + ''.join('%10.2f' % io_curves[k][i] for k in PLUGINS))
ax.set_xlabel('输入电平 (dBFS)')
ax.set_ylabel('增益衰减 GR (dB)')
ax.set_title('① 静态 IO 曲线')
ax.grid(alpha=.3)
ax.legend()

# ── 2. Attack ────────────────────────────────────────────
ax = axes[1]
print('\n=== Attack（-6 dBFS 硬起音）===')
print('%-8s' % 't(ms)' + ''.join('%10s' % k for k in PLUGINS))
atk_t = [1, 2, 3, 5, 8, 12, 20, 30, 50, 80, 120, 200]
atk_data = {}
for name, cfg in PLUGINS.items():
    dry, wet, sr = load(os.path.join(SIG, '03-attack.wav'), cfg['atk'], cfg['lat'])
    t = ATK_LEAD
    pts = []
    for ms in atk_t:
        k = t + ms * 1e-3
        gc = seg_db(wet, k, k + 0.001, sr) - seg_db(dry, k, k + 0.001, sr)
        pts.append(-(MAKEUP[name] - gc))
    atk_data[name] = np.array(pts)
    ax.plot(atk_t, pts, 'o-', color=cfg['color'], ms=4, lw=1.7, label=name)
for i, ms in enumerate(atk_t):
    print('%-8d' % ms + ''.join('%10.2f' % atk_data[k][i] for k in PLUGINS))
ax.set_xscale('log')
ax.set_xlabel('起音后时间 (ms)')
ax.set_ylabel('GR (dB)')
ax.set_title('② Attack —— 起音建立')
ax.grid(alpha=.3, which='both')
ax.legend()

# ── 3. Release ───────────────────────────────────────────
ax = axes[2]
print('\n=== Release（500 ms 过载后）===')
print('%-8s' % 't(ms)' + ''.join('%10s' % k for k in PLUGINS))
rel_t = [1, 5, 20, 60, 100, 200, 400, 800, 1600, 3200]
rel_data = {}
for name, cfg in PLUGINS.items():
    dry, wet, sr = load(os.path.join(SIG, '02-release.wav'), cfg['rel'], cfg['lat'])
    t = REL_LEAD
    end = t + 0.5
    probe_end = end + 8.0
    ref = seg_db(wet, probe_end - 0.5, probe_end, sr)
    pts = []
    for ms in rel_t:
        at = end + ms * 1e-3
        if at + 0.04 > probe_end:
            pts.append(np.nan); continue
        pts.append(seg_db(wet, at, at + 0.04, sr) - ref)
    rel_data[name] = np.array(pts)
    ax.plot(rel_t, pts, 'o-', color=cfg['color'], ms=4, lw=1.7, label=name)
for i, ms in enumerate(rel_t):
    print('%-8d' % ms + ''.join('%10.2f' % rel_data[k][i] for k in PLUGINS))
ax.set_xscale('log')
ax.set_xlabel('过载结束后时间 (ms)')
ax.set_ylabel('剩余 GR (dB)')
ax.set_title('③ Release —— 增益恢复')
ax.grid(alpha=.3, which='both')
ax.axhline(-3, color='#999', ls='--', lw=.8)
ax.legend()

fig.suptitle('TOiRE-2A（反馈式，实测校准） vs UAD LA-2A    Peak 拨盘 50 / makeup +14.5 dB',
             fontsize=12)
fig.tight_layout()
fig.savefig(os.path.join(REP, 'ours-vs-uad.png'), dpi=140)
plt.close(fig)

print('\n输出: docs/reports/ours-vs-uad.png')
