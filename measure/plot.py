# -*- coding: utf-8 -*-
"""
把 UAD LA-2A 的测量结果画成图并导出 CSV。

用法：
  python plot.py
输入：measure/signals/*.wav (dry) + measure/renders/R1-comp--*.wav (wet)
输出：docs/reports/*.png + *.csv
"""
import os, wave, csv
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

matplotlib.rcParams['font.sans-serif'] = ['Microsoft YaHei', 'SimHei', 'DejaVu Sans']
matplotlib.rcParams['axes.unicode_minus'] = False

BASE = r'E:\Programing\C++Hell\TOiRE-2A'
SIG = os.path.join(BASE, 'measure', 'signals')
REN = os.path.join(BASE, 'measure', 'renders')
REP = os.path.join(BASE, 'docs', 'reports')
os.makedirs(REP, exist_ok=True)

SR = 48000
LATENCY = 87

# ── 信号布局（与 gen-test-signals.py 一致）──
IO_LEAD, IO_STEP = 0.5, 1.5
LEVELS = list(range(-60, 1, 3))

REL_LEAD = 0.5
REL_CASES = [(0.5, 8.0), (0.02, 8.0), (2.0, 10.0)]

ATK_LEVELS = [-6.0, -12.0, -20.0]
ATK_LEAD, ATK_HOLD, ATK_GAP = 0.8, 2.0, 0.8


def read_wav(path):
    with wave.open(path, 'rb') as w:
        n, ch, sw, sr = w.getnframes(), w.getnchannels(), w.getsampwidth(), w.getframerate()
        raw = w.readframes(n)
    if sw == 3:
        a = np.frombuffer(raw, dtype=np.uint8).reshape(-1, 3).astype(np.int32)
        v = a[:, 0] | (a[:, 1] << 8) | (a[:, 2] << 16)
        v = np.where(v >= (1 << 23), v - (1 << 24), v).astype(np.float64) / float(1 << 23)
    else:
        v = np.frombuffer(raw, dtype='<i2').astype(np.float64) / float(1 << 15)
    if ch > 1:
        # 只看第 0 声道（见 analyze.py 里的说明）
        v = v.reshape(-1, ch)[:, 0]
    return v, sr


def rms_db(x, a, b):
    s, e = int(a), int(b)
    m = (e - s) // 6
    seg = x[s + m: e - m]
    if len(seg) == 0:
        return -120.0
    return float(20 * np.log10(max(np.sqrt(np.mean(seg ** 2)), 1e-12)))


def load(dry_name, wet_name):
    dry, sr = read_wav(os.path.join(SIG, dry_name))
    wet, _ = read_wav(os.path.join(REN, wet_name))
    wet = wet[LATENCY:]
    n = min(len(dry), len(wet))
    return dry[:n], wet[:n], sr


# ─────────────────────────────────────────────────────────────
# 1. IO 曲线
# ─────────────────────────────────────────────────────────────
def plot_io():
    dry, wet, sr = load('01-io-stairs.wav', 'R1-comp--01-io-stairs.wav')
    rows = []
    for i, lv in enumerate(LEVELS):
        a, b = (IO_LEAD + i * IO_STEP) * sr, (IO_LEAD + (i + 1) * IO_STEP) * sr
        d, w = rms_db(dry, a, b), rms_db(wet, a, b)
        rows.append((lv, d, w, w - d))

    # makeup = 线性区增益
    makeup = float(np.mean([g for lv, d, w, g in rows if lv <= -30]))

    # CSV（GR = makeup − 增益变化）
    with open(os.path.join(REP, 'io-curve.csv'), 'w', newline='', encoding='utf-8-sig') as f:
        cw = csv.writer(f)
        cw.writerow(['输入_dBFS', '干_RMS_dBFS', '湿_RMS_dBFS', '增益变化_dB', 'GR_dB'])
        for lv, d, w, g in rows:
            cw.writerow([lv, round(d, 2), round(w, 2), round(g, 2), round(makeup - g, 2)])

    xs = [r[0] for r in rows]
    ys = [r[2] for r in rows]
    gr = [makeup - r[3] for r in rows]

    fig, ax = plt.subplots(1, 2, figsize=(13, 5))
    ax[0].plot(xs, xs, '--', color='#999', label='1:1（不压缩）')
    ax[0].plot(xs, [y - makeup for y in ys], 'o-', color='#c0392b', ms=4,
               label='UAD LA-2A 输出（已减去 makeup）')
    ax[0].set_xlabel('输入电平 (dBFS)'); ax[0].set_ylabel('输出电平 (dBFS)')
    ax[0].set_title('静态 IO 曲线  (Peak=50, Gain=50, Comp)')
    ax[0].grid(alpha=.3); ax[0].legend(); ax[0].set_xlim(-62, 2); ax[0].set_ylim(-62, 2)

    ax[1].plot(xs, gr, 'o-', color='#2980b9', ms=4)
    ax[1].set_xlabel('输入电平 (dBFS)'); ax[1].set_ylabel('增益衰减 GR (dB)')
    ax[1].set_title('增益衰减 vs 输入电平  （makeup = %+.2f dB）' % makeup)
    ax[1].grid(alpha=.3); ax[1].axhline(0, color='#999', lw=.8)
    ax[1].annotate('阈值 ≈ −27 dBFS', xy=(-27, 0), xytext=(-48, 4),
                   arrowprops=dict(arrowstyle='->', color='#666'))

    fig.tight_layout()
    fig.savefig(os.path.join(REP, 'io-curve.png'), dpi=140)
    plt.close(fig)
    print('io-curve.png / .csv    makeup = %+.2f dB' % makeup)
    return makeup


# ─────────────────────────────────────────────────────────────
# 2. Attack
# ─────────────────────────────────────────────────────────────
def plot_attack(makeup):
    dry, wet, sr = load('03-attack.wav', 'R1-comp--03-attack.wav')
    t = ATK_LEAD
    fig, ax = plt.subplots(figsize=(9, 5))
    allrows = []
    for lv in ATK_LEVELS:
        pts = []
        for ms in [0.5, 1, 2, 3, 5, 8, 12, 20, 30, 50, 80, 120, 200]:
            k = t + ms * 1e-3
            d = rms_db(dry, k * sr, (k + 0.001) * sr)
            w = rms_db(wet, k * sr, (k + 0.001) * sr)
            pts.append((ms, makeup - (w - d)))
            allrows.append([lv, ms, round(makeup - (w - d), 3)])
        ax.plot([p[0] for p in pts], [p[1] for p in pts], 'o-', ms=3, label='%g dBFS' % lv)
        t += ATK_HOLD + ATK_GAP

    ax.set_xscale('log')
    ax.set_xlabel('起音后时间 (ms)'); ax.set_ylabel('增益衰减 GR (dB)')
    ax.set_title('Attack：硬起音后 GR 的建立')
    ax.grid(alpha=.3, which='both'); ax.legend(title='起音电平')

    with open(os.path.join(REP, 'attack.csv'), 'w', newline='', encoding='utf-8-sig') as f:
        cw = csv.writer(f); cw.writerow(['起音_dBFS', 't_ms', 'GR_dB'])
        cw.writerows(allrows)

    fig.tight_layout(); fig.savefig(os.path.join(REP, 'attack.png'), dpi=140); plt.close(fig)
    print('attack.png / .csv')


# ─────────────────────────────────────────────────────────────
# 3. Release
# ─────────────────────────────────────────────────────────────
def plot_release():
    dry, wet, sr = load('02-release.wav', 'R1-comp--02-release.wav')
    t = REL_LEAD
    fig, ax = plt.subplots(figsize=(9, 5))
    allrows = []
    for burst_s, probe_s in REL_CASES:
        end = t + burst_s
        probe_end = end + probe_s
        ref = rms_db(wet, probe_end - 0.5 * sr, probe_end)
        ms_list = [1, 2, 5, 10, 20, 40, 60, 100, 200, 400, 800, 1600, 3200, 6400]
        pts = []
        for ms in ms_list:
            at = end + ms * 1e-3
            if at + 0.04 > probe_end:
                break
            w = rms_db(wet, at * sr, (at + 0.04) * sr)
            pts.append((ms, w - ref))
            allrows.append([burst_s * 1000, ms, round(w - ref, 3)])
        ax.plot([p[0] for p in pts], [p[1] for p in pts], 'o-', ms=3,
                label='过载 %g ms' % (burst_s * 1000))
        t = probe_end

    ax.set_xscale('log')
    ax.set_xlabel('过载结束后时间 (ms)'); ax.set_ylabel('剩余 GR (dB)')
    ax.set_title('Release：增益恢复（program dependent）')
    ax.grid(alpha=.3, which='both'); ax.legend()
    ax.axhline(-3, color='#999', ls='--', lw=.8)

    with open(os.path.join(REP, 'release.csv'), 'w', newline='', encoding='utf-8-sig') as f:
        cw = csv.writer(f); cw.writerow(['过载_ms', 't_ms', '剩余GR_dB'])
        cw.writerows(allrows)

    fig.tight_layout(); fig.savefig(os.path.join(REP, 'release.png'), dpi=140); plt.close(fig)
    print('release.png / .csv')


if __name__ == '__main__':
    m = plot_io()
    plot_attack(m)
    plot_release()
    print('\n输出目录:', REP)
