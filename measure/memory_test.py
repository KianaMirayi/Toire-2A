# -*- coding: utf-8 -*-
"""
验证「记忆性」：恢复速度是否随先前压缩强度变化

信号 02-release.wav 里有三种过载长度：
  a) 500 ms   b) 20 ms   c) 2000 ms
每种过载后接一段 −45 dBFS 的探针音（低于阈值，不触发压缩），
探针段的输出电平直接反映当前剩余的 GR。

关键指标：
  GR_start   过载结束时的 GR      —— 压得越深，说明累积越多
  恢复 50%   从 GR_start 掉一半所需时间 —— 记忆性的核心
"""
import os
import numpy as np

BASE = r'E:\Programing\C++Hell\TOiRE-2A'
SIG = os.path.join(BASE, 'measure', 'signals', '02-release.wav')
REN_UAD = os.path.join(BASE, 'measure', 'renders', 'R1-comp--02-release.wav')
REN_OURS = os.path.join(BASE, 'measure', 'renders', 'ours', 'ours--02-release.wav')

SR = 48000
LEAD = 0.5
CASES = [(0.5, 8.0), (0.02, 8.0), (2.0, 10.0)]


def read_wav(path):
    with open(path, 'rb') as f:
        f.read(4); f.seek(12)
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


def seg_db(x, a, b, sr):
    s, e = int(a * sr), int(b * sr)
    return 20 * np.log10(max(np.sqrt(np.mean(x[s:e] ** 2)), 1e-12))


def analyse(path, lat, label):
    dry, sr = read_wav(SIG)
    wet, _ = read_wav(path)
    if lat > 0:
        wet = np.concatenate([wet[lat:], np.zeros(lat)])
    n = min(len(dry), len(wet))
    dry, wet = hp(dry[:n], sr), hp(wet[:n], sr)

    print('\n=== %s ===' % label)
    t = LEAD
    for burst, probe in CASES:
        end = t + burst
        probe_end = end + probe
        ref = seg_db(wet, probe_end - 0.6, probe_end, sr)     # 完全恢复后的电平

        # 过载结束时（探针刚开始）的 GR
        g0 = ref - seg_db(wet, end + 0.002, end + 0.012, sr)

        # 采样恢复曲线
        ms_list = [1, 2, 5, 10, 20, 40, 60, 100, 150, 200, 300, 400, 600, 800,
                   1200, 1600, 2400, 3200, 4800]
        curve = []
        for ms in ms_list:
            at = end + ms * 1e-3
            if at + 0.03 > probe_end:
                break
            curve.append((ms, ref - seg_db(wet, at, at + 0.03, sr)))

        # 找 50% 恢复时间
        half = g0 * 0.5
        t50 = None
        for i in range(1, len(curve)):
            if curve[i][1] <= half:
                (m0, g_hi), (m1, g_lo) = curve[i - 1], curve[i]
                t50 = m0 + (m1 - m0) * (g_hi - half) / max(g_hi - g_lo, 1e-9)
                break

        print('  过载 %5.0f ms → 起始 GR %5.2f dB   恢复50%% %s' %
              (burst * 1000, g0,
               ('%6.0f ms' % t50) if t50 else '  >4800 ms'))
        t = probe_end


analyse(REN_UAD, 87, 'UAD LA-2A')
analyse(REN_OURS, 0, 'TOiRE-2A（当前）')
print('\n如果「起始 GR 越大 → 恢复越慢」成立，说明记忆性机制在工作。')
