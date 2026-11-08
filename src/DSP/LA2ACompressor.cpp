#include "LA2ACompressor.h"

namespace toreia2a
{

namespace
{
// 侧链电平 → GR 的柔化拐点（保证 GR 在拐点处正好从 0 开始）
constexpr float kHalfLn2 = 0.6931472f;
} // namespace

void LA2ACompressor::prepare (double sr) noexcept
{
    sampleRate = (sr > 0.0) ? sr : 48000.0;
    reset();

    cAttackFast  = coefficientFor (kAttackFastMs);
    cAttackSlow  = coefficientFor (kAttackSlowMs);
    cEnv         = coefficientFor (2.0f);
    cMemRise     = coefficientFor (kMemRiseMs);
    cMemFall     = coefficientFor (kMemFallMs);

    // ── 侧链频率响应（UAD 实测 H(f) 的最小二乘拟合）─────────
    // 拟合误差 RMS 0.22 dB / 最大 0.62 dB，见 docs/R37-侧链测量.md
    //   ① low shelf  349 Hz  +4.35 dB
    //   ② peaking   4820 Hz  Q=0.71  +1.07 dB
    //   ③ high shelf 7405 Hz -1.12 dB
    scEq[0].b0 = 1.008141441f; scEq[0].b1 = -1.942478642f; scEq[0].b2 = 0.936944970f;
    scEq[0].a1 = -1.942992269f; scEq[0].a2 = 0.944572784f;

    scEq[1].b0 = 1.036887715f; scEq[1].b1 = -1.161711399f; scEq[1].b2 = 0.401853006f;
    scEq[1].a1 = -1.161711399f; scEq[1].a2 = 0.438740721f;

    scEq[2].b0 = 0.916537735f; scEq[2].b1 = -0.625363503f; scEq[2].b2 = 0.233812766f;
    scEq[2].a1 = -0.747016138f; scEq[2].a2 = 0.272003136f;
}

void LA2ACompressor::reset() noexcept
{
    for (auto& bq : scEq)
        bq.reset();
    brightFast  = 0.0f;
    brightSlow  = 0.0f;
    r1          = 0.0f;
    pEnv        = 0.0f;
    dcState     = 0.0f;
    dcPrev      = 0.0f;

    currentGain = 1.0f;
    lastGrDb    = 0.0f;
    inPeak      = 0.0f;
    outPeak     = 0.0f;
}

float LA2ACompressor::coefficientFor (float ms) const noexcept
{
    if (ms <= 0.0f)
        return 1.0f;
    const auto n = ms * 0.001f * static_cast<float> (sampleRate);
    return 1.0f - std::exp (-1.0f / std::max (n, 1.0f));
}

float LA2ACompressor::softPlus (float x, float soft) noexcept
{
    // soft * ln(1 + e^(x/soft))，避免溢出
    if (x > 30.0f * soft)
        return x;
    if (x < -30.0f * soft)
        return 0.0f;
    return soft * std::log1p (std::exp (x / soft));
}

void LA2ACompressor::setPeakReduction (float n) noexcept
{
    peakReduction = std::clamp (n, 0.0f, 1.0f);

    // Peak Reduction 控制侧链驱动量（手册 Section 16）。握手关系【不是线性的】：
    // 实测 UAD 的 GR vs 拨盘是加速上升的，线性映射在高档位会严重不足
    //   Peak 40 → 7.2 dB     Peak 60 → 12.8 dB     Peak 80 → 21.9 dB
    // 用线性 30dB×peak 时 Peak 80 只能到 14.6 dB（少 7.3 dB），
    // 压缩量不足 → 按 τ=A/GR 恢复就快 → 听感上出现"回声"。
    // 下表由「UAD 实测 GR 反解所需的侧链增益」得到。
    // 第一版由反解得到，第二版按实测 GR 偏差逐点修正（浅压缩区压多了 1.2 dB）
    static const float peakMap[9][2] = {
        {   0.0f, -3.9f },
        {  20.0f, -2.7f },
        {  30.0f,  2.7f },
        {  40.0f, 10.0f },
        {  50.0f, 14.7f },
        {  60.0f, 19.5f },
        {  70.0f, 27.8f },
          {  80.0f, 34.2f },
        { 100.0f, 39.5f },
    };
    const float dial = peakReduction * 100.0f;
    float db = peakMap[8][1];
    for (int i = 1; i < 9; ++i)
    {
        if (dial <= peakMap[i][0])
        {
            const float t = (dial - peakMap[i - 1][0]) / (peakMap[i][0] - peakMap[i - 1][0]);
            db = peakMap[i - 1][1] + t * (peakMap[i][1] - peakMap[i - 1][1]);
            break;
        }
    }
    peakGainDb = db;
}

void LA2ACompressor::setMakeupGainDb (float db) noexcept
{
    makeupGainDb = std::clamp (db, -100.0f, 40.0f);
    makeupGain   = std::pow (10.0f, makeupGainDb / 20.0f);
}

void LA2ACompressor::updateSidechain (float sample) noexcept
{
    // ── 1) 侧链频率响应（实测拟合的三段双二阶级联）────────
    // 注意：必须作用在【有符号的音频信号】上，再整流。
    // 如果先整流再滤波，整流信号的能量集中在 DC/2f，低架滤波会变成
    // 全频段 +3.5 dB 的提升（实测踩过这个坑）。
    if (scEqEnabled)
        for (auto& bq : scEq)
            sample = bq.process (sample);

    const float det = std::fabs (sample);

    // ── 2) 侧链驱动（Peak Reduction 的增益）─────────────
    const float x = det * std::pow (10.0f, peakGainDb / 20.0f);

    // ── 3) attack：跳跃越大充得越快（真机 EL 面板被强信号瞬间点亮）──
    // 固定时间常数会让短过载充不满（20 ms 过载 GR 只有 5.81，UAD 是 8.42）。
    // 用「目标/当前」的幅度比来加速：从静音起跳时 atkSpeed 很大 → 几乎瞬时。
    const float p = x * x;
    const float pDet = p;   // 直接跟瞬时功率（加平均会引入 3 dB 电平偏差）
    const float brightPrev = kAttackMix * brightFast + (1.0f - kAttackMix) * brightSlow;
    const float jump = (brightPrev > 1e-9f) ? (p / brightPrev) : 1.0e6f;
    const float atkSpeed = std::clamp (std::sqrt (jump), 1.0f, 1.0f);   // 上限 1 = 关闭加速（试过提速，反而让 bright 跟着波形抖动，GR 变浅）
    const float cAtkFastNow = coefficientFor (kAttackFastMs / atkSpeed);
    const float cAtkSlowNow = coefficientFor (kAttackSlowMs / atkSpeed);

    // ── 记忆深度 → 释放速度（真机：CdS 被压得越深，恢复越慢）──
    // 用 r3 / bright 的【比例】而不是绝对 dB：
    //   20 ms 过载 → r3 只充到 bright 的 3%  → 几乎无记忆 → 快速恢复
    //   2000 ms 过载 → r3 接近 bright        → 强记忆   → 慢速恢复
    // （早前用绝对 dB 映射，-70 dB 的跨度让 3% 也算"很亮"，缩放失效）
    const float memRatio = (brightPrev > 1e-9f) ? (r1 / brightPrev) : 0.0f;
    const float memN  = std::clamp (memRatio * 2.5f, 0.0f, 1.0f);

    // ── 释放：双曲线律（光敏电阻的实际恢复特性）──────────
    // 实测 UAD 的释放曲线几乎完美符合 GR(t) = A/(t+t0)，A = 1126 dB·ms，t0 = 121 ms
    // （单指数拟合的残差是它的 4.7 倍）。
    //   GR ∝ 1/t  ⟺  dGR/dt = −GR²/A  ⟺  等效时间常数 τ = A/GR
    // 即「压得越深恢复越快，越接近恢复完越慢」—— 指数衰减做不出这个形状。
    //
    // A 还要随记忆深度变化：短过载恢复快（A 小），长过载恢复慢（A 大）。
    // 实测：20 ms 过载 t50 = 7 ms，500 ms → 64 ms，2000 ms → 68 ms。
    // 判据用【实际施加的增益衰减】，不是开环 GR：
    //   lastGrDb 是 GR 曲线算出的开环值，闭环后实际约小 4.8 倍（1+slope）。
    //   用开环值会让 τ 被算小近 5 倍 → 尾巴掉得快。
    const float grDepth = std::max (0.0f,
                                    -20.0f * std::log10 (std::max (currentGain, 1.0e-6f)));
    const float aMs   = kRelQuadA * (kRelQuadAMin + (1.0f - kRelQuadAMin) * memN);
    const float tauMs = (grDepth > 0.2f)
                          ? std::clamp (aMs / std::pow (grDepth, kRelTauExp) + kRelTauFloorMs, kRelTauMinMs, kRelTauMaxMs)
                          : kRelTauMaxMs;
    const float cRelNow = coefficientFor (tauMs);

    const float cbF = (pDet > brightFast) ? cAtkFastNow : cRelNow;
    brightFast += (pDet - brightFast) * cbF;

    const float cbS = (pDet > brightSlow) ? cAtkSlowNow : cRelNow;
    brightSlow += (pDet - brightSlow) * cbS;

    const float bright = kAttackMix * brightFast + (1.0f - kAttackMix) * brightSlow;

    // ── 4) 记忆状态（独立，不参与电平）──────────────────
    // 慢充慢放，用来表示「之前累积了多少光量」。
    // 三段并联释放试过：要么反馈失控（GR 冲到 39 dB），要么同步后记忆判据失效。
    // 现阶段用「单段释放 + 独立记忆调制」最稳。
    r1 += (bright - r1) * ((bright > r1) ? cMemRise : cMemFall);

    // ── 5) 电平：单段（余辉不再参与求和）──────────────────
    const float cds = bright;


    const float lvlDb  = 10.0f * std::log10 (cds + 1e-12f);   // = 20log10(|x|) 的等效

    // ── 6) 静态 GR 曲线：软拐点 + 线性斜率 ──────────────
    const float over  = lvlDb - kKneeDb;

    // ── 两段斜率：单一线性斜率拟合不出"低电平偏深、高电平偏浅"的曲线 ──
    // 实测矩阵显示我们的误差符号随电平翻转，说明真实曲线是【凸的】（加速）：
    //   主斜率 = kSlope      → 控制中低电平
    //   附加斜率 = kSlope2   → 只在 over 超过 kKnee2Db 后生效（高电平）
    float grDb = kSlope  * (softPlus (over, kSoftDb) - kSoftDb * kHalfLn2);
    grDb += kSlope2 * softPlus (over - kKnee2Db, kSoft2Db);   // 低于第二拐点时自动趋近 0
    grDb = std::max (0.0f, grDb);

    currentGain = std::pow (10.0f, -grDb / 20.0f);
    lastGrDb    = -grDb;
}

float LA2ACompressor::process (float* const* channels, int numChannels, int numSamples) noexcept
{
    if (channels == nullptr || numChannels <= 0 || numSamples <= 0)
        return 0.0f;

    inPeak  = 0.0f;
    outPeak = 0.0f;

    float blockMaxGr = 0.0f;
    float feedback   = 0.0f;      // 上一次【增益衰减后、makeup 之前】的信号

    for (int n = 0; n < numSamples; ++n)
    {
        // 侧链读「上一采样点的增益衰减后信号」——反馈拓扑
        // 关键：不含 makeup！真机的侧链接在 Gain 旋钮之前，
        // 所以 Gain 不影响压缩量（手册明确写"Gain 控制不影响压缩"）。
        // 之前把含 makeup 的输出喂给侧链，导致 Gain 调小后压缩直接塌掉。
        updateSidechain (feedback);

        float nextFeedback = 0.0f;

        for (int ch = 0; ch < numChannels; ++ch)
        {
            const float in   = channels[ch][n];
            const float gain = in * currentGain;          // 增益衰减后（侧链取这个）
            float out        = gain * makeupGain;         // 带 makeup

            // ── 输出级软饱和（变压器 + 电子管染色）──────────
            // 实测 UAD（Peak=Min，Gain 50）：−12 dBFS 起开始压缩，
            // 0 dBFS 时基波损失 8.3 dB，THD 从 −49 dB 升到 −25 dB。
            // 之前完全没有染色，THD 只有 −86~−190 dB，所以听起来"太干净"。
            // ── 输出级饱和：软限幅 + 偶次谐波项（拟合 UAD 的 H2–H7）──
            //   T = 1.612（拐点 4.15 dBFS）  n = 4.63
            //   a2 = +0.0331（二次谐波）      a4 = -0.0268（四次谐波）
            const float u  = out / kSatT;
            const float au = std::fabs (u);
            const float y  = kSatT * u * std::pow (1.0f + std::pow (au, kSatN), -1.0f / kSatN);
            const float y2 = y * y;
            const float y3 = y2 * y;
            const float y4 = y2 * y2;
            const float y5 = y4 * y;
            const float sat = y + kSatA2 * y2 + kSatA3 * y3 + kSatA4 * y4 + kSatA5 * y5;

            // 一阶直流阻断：不对称项会引入直流
            dcState = sat - dcPrev + 0.995f * dcState;
            dcPrev  = sat;
            out = dcState;

            channels[ch][n] = out;

            inPeak = std::max (inPeak, std::fabs (in));
            // 取幅度最大的那个声道，保留符号（侧链滤波需要原始波形）
            if (std::fabs (gain) > std::fabs (nextFeedback))
                nextFeedback = gain;
        }

        feedback = nextFeedback;
        outPeak  = std::max (outPeak, std::fabs (channels[0][n]));
        blockMaxGr = std::min (blockMaxGr, lastGrDb);
    }

    return blockMaxGr;
}

} // namespace toreia2a
