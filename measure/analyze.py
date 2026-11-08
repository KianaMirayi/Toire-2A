# -*- coding: utf-8 -*-
"""
LA-2A 测量分析器

用法：
  python analyze.py io      --dry dry.wav --wet wet.wav
  python analyze.py release --dry dry.wav --wet wet.wav
  python analyze.py attack  --dry dry.wav --wet wet.wav
  python analyze.py freq    --dry dry.wav --wet wet.wav

dry = 未处理的输入（我生成的测试信号）
wet = 过插件之后导出的结果
"""
import sys, os, wave
import numpy as np

# ── 测试信号布局（与 gen-test-signals.py 严格对应）──────────────
SR = 48000
LEAD = 0.5                      # 前置静音
STEP_S = 1.5                    # 每档时长
LEVELS = list(range(-60, 1, 3)) # 21 档

REL_CASES = [                   # (过载时长, 探针时长)
    (0.5, 8.0),
    (0.02, 8.0),
    (2.0, 10.0),
]
REL_LEAD = 0.5

ATK_LEVELS = [-6.0, -12.0, -20.0]
ATK_LEAD = 0.8
ATK_HOLD = 2.0
ATK_GAP = 0.8

FREQS = [100, 200, 500, 1000, 2000, 4000, 8000, 12000]
FREQ_LEAD = 0.3
FREQ_S = 1.5


def read_wav(path):
    with wave.open(path, 'rb') as w:
        n, ch, sw, sr = w.getnframes(), w.getnchannels(), w.getsampwidth(), w.getframerate()
        raw = w.readframes(n)
    if sw == 3:
        a = np.frombuffer(raw, dtype=np.uint8).reshape(-1, 3).astype(np.int32)
        v = a[:, 0] | (a[:, 1] << 8) | (a[:, 2] << 16)
        v = np.where(v >= (1 << 23), v - (1 << 24), v).astype(np.float64) / float(1 << 23)
    elif sw == 2:
        v = np.frombuffer(raw, dtype='<i2').astype(np.float64) / float(1 << 15)
    elif sw == 4:
        v = np.frombuffer(raw, dtype='<i4').astype(np.float64) / float(1 << 31)
    else:
        raise ValueError("unsupported sample width %d" % sw)
    if ch > 1:
        # 只看第 0 声道：我们喂的是单声道，插件输出 ch1 可能是空的，
        # 按多声道平均会凭空掉 3 dB（曾经因此把 +14.50 算成 +8.48）
        v = v.reshape(-1, ch)[:, 0]
    return v, sr


def db(x):
    return 20.0 * np.log10(np.maximum(np.abs(x), 1e-12))


def seg_rms_db(x, a, b):
    """[a,b) 区间中间 70% 的 RMS，转 dB"""
    s, e = int(a), int(b)
    m = (e - s) // 6
    seg = x[s + m : e - m]
    if len(seg) == 0:
        return -120.0
    return float(db(np.sqrt(np.mean(seg ** 2))))


def align(dry, wet, sr, forced=0):
    """延迟对齐。forced>0 时直接使用宿主报告的延迟。"""
    if forced:
        lag = forced
        if lag > 0:
            wet = wet[lag:]
        elif lag < 0:
            wet = np.concatenate([np.zeros(-lag), wet])
        return wet, lag
    n = min(len(dry), len(wet), SR * 2)
    if n < 1000:
        return wet, 0
    c = np.correlate(wet[:n], dry[:n], mode='full')
    lag = int(np.argmax(c)) - (n - 1)
    if lag > 0:
        wet = wet[lag:]
    elif lag < 0:
        wet = np.concatenate([np.zeros(-lag), wet])
    return wet, lag


def report_io(dry, wet, sr):
    print("\n%-10s %10s %10s %10s %10s" % ("输入dB", "干RMS", "湿RMS", "增益变化", "GR估计"))
    print("-" * 56)
    rows = []
    for i, lv in enumerate(LEVELS):
        a = (LEAD + i * STEP_S) * sr
        b = (LEAD + (i + 1) * STEP_S) * sr
        d = seg_rms_db(dry, a, b)
        w_ = seg_rms_db(wet, a, b)
        diff = w_ - d
        rows.append((lv, d, w_, diff))
        print("%-10d %10.2f %10.2f %+10.2f %10.2f" % (lv, d, w_, diff, -diff))
    return rows


def report_release(dry, wet, sr):
    """过载后接探针音：探针段的输出电平直接反映增益恢复"""
    t = REL_LEAD
    print("\n=== Release：过载结束后的增益恢复 ===")
    print("（探针音 -45 dBFS；表格里的负值 = 相对于完全恢复还差多少 dB）")
    for burst_s, probe_s in REL_CASES:
        end = t + burst_s
        probe_end = end + probe_s
        ref = seg_rms_db(wet, probe_end - 0.5 * sr, probe_end)   # 完全恢复后的电平
        dry_ref = seg_rms_db(dry, probe_end - 0.5 * sr, probe_end)
        print("\n--- 过载 %.0f ms ---" % (burst_s * 1000))
        print("   完全恢复后: 湿=%.2f dB  干=%.2f dB  (makeup=%+.2f)" % (ref, dry_ref, ref - dry_ref))
        print("   %8s %10s %12s" % ("t(ms)", "湿RMS", "剩余GR"))
        for ms in [1, 2, 5, 10, 20, 40, 60, 100, 200, 400, 800, 1600, 3200, 6400]:
            at = end + ms * 1e-3
            if at + 0.04 > probe_end:
                break
            w_ = seg_rms_db(wet, at * sr, (at + 0.04) * sr)
            print("   %8d %10.2f %+12.2f" % (ms, w_, w_ - ref))
        t = probe_end


def report_attack(dry, wet, sr):
    print("\n=== Attack：硬起音后增益衰减的建立 ===")
    t = ATK_LEAD
    for lv in ATK_LEVELS:
        print("\n--- 起音 %.0f dBFS ---" % lv)
        seg = slice(int(t * sr), int((t + 0.2) * sr))
        d = db(np.abs(dry[seg]))
        w_ = db(np.abs(wet[seg]))
        # 用包络差近似 GR
        print("   %8s %10s %10s" % ("t(ms)", "差值dB", "说明"))
        for ms in [0.5, 1, 2, 3, 5, 8, 12, 20, 30, 50, 80, 120]:
            k = int(ms * 1e-3 * sr)
            if k >= len(d) - 10:
                break
            win = slice(k, k + int(1e-3 * sr))
            diff = np.mean(w_[win] - d[win])
            print("   %8.1f %10.2f" % (ms, diff))
        t += ATK_HOLD + ATK_GAP


def report_freq(dry, wet, sr):
    print("\n=== 侧链频率响应：各频率下的稳态 GR ===")
    print("%-10s %10s %10s %10s" % ("频率Hz", "干RMS", "湿RMS", "增益变化"))
    print("-" * 44)
    t = FREQ_LEAD
    for f in FREQS:
        a, b = t * sr, (t + FREQ_S) * sr
        d = seg_rms_db(dry, a, b)
        w_ = seg_rms_db(wet, a, b)
        print("%-10d %10.2f %10.2f %+10.2f" % (f, d, w_, w_ - d))
        t += FREQ_S


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return
    kind = sys.argv[1]
    argv = sys.argv[2:]
    args = {}
    i = 0
    while i < len(argv) - 1:
        if argv[i].startswith('--'):
            args[argv[i]] = argv[i + 1]
            i += 2
        else:
            i += 1
    forced = int(args.get('--latency', '0'))
    dry, sr1 = read_wav(args['--dry'])
    wet, sr2 = read_wav(args['--wet'])
    print("dry: %.2f s @ %d Hz   wet: %.2f s @ %d Hz" % (len(dry)/sr1, sr1, len(wet)/sr2, sr2))

    wet, lag = align(dry, wet, sr1, forced)
    print("延迟对齐: %d 采样 (%.2f ms)%s" % (lag, lag * 1000.0 / sr1, "  [指定]" if forced else "  [自动]"))

    n = min(len(dry), len(wet))
    dry, wet = dry[:n], wet[:n]

    {'io': report_io, 'release': report_release,
     'attack': report_attack, 'freq': report_freq}[kind](dry, wet, sr1)


if __name__ == '__main__':
    main()
