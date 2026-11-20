#include "LA2ACompressor.h"

namespace toreia2a
{

namespace
{
// 侧链电平 → GR 的柔化拐点（保证 GR 在拐点处正好从 0 开始）
constexpr float kHalfLn2 = 0.6931472f;
} // namespace

namespace
{
    // DSP 层刻意不依赖 JUCE，所以自带一个 π
    constexpr double kPi = 3.14159265358979323846;

    // ── RBJ cookbook 双二阶设计（只用于非 48 kHz；48 kHz 用实测拟合系数）──
    // 与 Biquad::process 的符号约定一致：a1/a2 是标准分母系数（a0 归一化到 1）
    void designLowShelf (float& o0, float& o1, float& o2,
                         float& a1, float& a2,
                         float f, float gainDb, float q, double sr)
    {
        const double A  = std::pow (10.0, gainDb / 40.0);
        const double w  = 2.0 * kPi * f / sr;
        const double cs = std::cos (w), sn = std::sin (w);
        const double al = sn / (2.0 * q);
        const double sq = 2.0 * std::sqrt (A) * al;
        const double a0 = (A + 1.0) + (A - 1.0) * cs + sq;
        o0 = (float) (A * ((A + 1.0) - (A - 1.0) * cs + sq) / a0);
        o1 = (float) (2.0 * A * ((A - 1.0) - (A + 1.0) * cs) / a0);
        o2 = (float) (A * ((A + 1.0) - (A - 1.0) * cs - sq) / a0);
        a1 = (float) (-2.0 * ((A - 1.0) + (A + 1.0) * cs) / a0);
        a2 = (float) (((A + 1.0) + (A - 1.0) * cs - sq) / a0);
    }

    void designHighShelf (float& o0, float& o1, float& o2,
                          float& a1, float& a2,
                          float f, float gainDb, float q, double sr)
    {
        const double A  = std::pow (10.0, gainDb / 40.0);
        const double w  = 2.0 * kPi * f / sr;
        const double cs = std::cos (w), sn = std::sin (w);
        const double al = sn / (2.0 * q);
        const double sq = 2.0 * std::sqrt (A) * al;
        const double a0 = (A + 1.0) - (A - 1.0) * cs + sq;
        o0 = (float) (A * ((A + 1.0) + (A - 1.0) * cs + sq) / a0);
        o1 = (float) (-2.0 * A * ((A - 1.0) + (A + 1.0) * cs) / a0);
        o2 = (float) (A * ((A + 1.0) + (A - 1.0) * cs - sq) / a0);
        a1 = (float) (2.0 * ((A - 1.0) - (A + 1.0) * cs) / a0);
        a2 = (float) (((A + 1.0) - (A - 1.0) * cs - sq) / a0);
    }

    void designPeaking (float& o0, float& o1, float& o2,
                        float& a1, float& a2,
                        float f, float gainDb, float q, double sr)
    {
        const double A  = std::pow (10.0, gainDb / 40.0);
        const double w  = 2.0 * kPi * f / sr;
        const double cs = std::cos (w), sn = std::sin (w);
        const double al = sn / (2.0 * q);
        const double a0 = 1.0 + al / A;
        o0 = (float) ((1.0 + al * A) / a0);
        o1 = (float) (-2.0 * cs / a0);
        o2 = (float) ((1.0 - al * A) / a0);
        a1 = (float) (-2.0 * cs / a0);
        a2 = (float) ((1.0 - al / A) / a0);
    }

    /** 48 kHz 之外的采样率：按设计参数重新设计侧链 EQ。
        48 kHz 本身保持实测拟合系数不变（保证标定结果位级一致）。*/
    bool isCalibrationRate (double sr) noexcept { return std::abs (sr - 48000.0) < 0.5; }
}

void LA2ACompressor::prepare (double sr) noexcept
{
    sampleRate = (sr > 0.0) ? sr : 48000.0;
    reset();

    cAttackFast  = coefficientFor (kAttackFastMs);
    cAttackSlow  = coefficientFor (kAttackSlowMs);
    cMemRise     = coefficientFor (kMemRiseMs);
    cMemFall     = coefficientFor (kMemFallMs);

    // 平滑器直接吸附到目标值：避免启动 / 换采样率时前几毫秒的渐入
    linearPeakGain = targetPeakGain;
    makeupGain     = targetMakeupGain;

    // 侧链频率响应：UAD 实测 H(f) 的最小二乘拟合（RMS 误差 0.22 dB）
    // ① low shelf 349Hz +4.35  ② peaking 4820Hz Q0.71 +1.07  ③ high shelf 7405Hz -1.12
    //
    // 下面的系数是【48 kHz 标定值】。48 kHz 时原样使用，保证标定结果位级不变；
    // 其它采样率按同一组设计参数用 RBJ 重新设计，否则频点会按 sr/48k 线性漂移
    // （96 kHz 时 349 Hz 会变成 698 Hz，压缩量与 UAD 对不上）。
    if (isCalibrationRate (sampleRate))
    {
        scEq[0].b0 = 1.008141441f; scEq[0].b1 = -1.942478642f; scEq[0].b2 = 0.936944970f;
        scEq[0].a1 = -1.942992269f; scEq[0].a2 = 0.944572784f;

        scEq[1].b0 = 1.036887715f; scEq[1].b1 = -1.161711399f; scEq[1].b2 = 0.401853006f;
        scEq[1].a1 = -1.161711399f; scEq[1].a2 = 0.438740721f;

        scEq[2].b0 = 0.916537735f; scEq[2].b1 = -0.625363503f; scEq[2].b2 = 0.233812766f;
        scEq[2].a1 = -0.747016138f; scEq[2].a2 = 0.272003136f;
    }
    else
    {
        designLowShelf  (scEq[0].b0, scEq[0].b1, scEq[0].b2, scEq[0].a1, scEq[0].a2,
                         349.0f,  4.35f, 0.707f, sampleRate);
        designPeaking   (scEq[1].b0, scEq[1].b1, scEq[1].b2, scEq[1].a1, scEq[1].a2,
                         4820.0f, 1.07f, 0.71f,  sampleRate);
        designHighShelf (scEq[2].b0, scEq[2].b1, scEq[2].b2, scEq[2].a1, scEq[2].a2,
                         7405.0f, -1.12f, 0.707f, sampleRate);
    }

    // 直流阻断极点：写死 0.995 相当于 48 kHz 下 38.3 Hz 转折。
    // 同样只在 48 kHz 保留原值，其它采样率按频率换算，避免高采样率把低频多削一截。
    dcCoef = isCalibrationRate (sampleRate)
               ? 0.995f
               : (float) std::exp (-2.0 * kPi * 38.3 / sampleRate);
}

void LA2ACompressor::reset() noexcept
{
    for (auto& bq : scEq)
        bq.reset();
    brightFast  = 0.0f;
    brightSlow  = 0.0f;
    r1          = 0.0f;
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

    // Peak Reduction 控制侧链驱动量，实测 GR vs 拨盘是【加速上升】的曲线
    // 线性映射时 Peak 80 只能到 14.6 dB（实测 21.9）→ 压缩不足、听感有回声
    // 下表由 UAD 实测 GR 反解侧链增益得到，并逐点修正了浅压缩区
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
    targetPeakGain = std::pow (10.0f, db / 20.0f);   // 缓存，避免每采样一次 pow
}

void LA2ACompressor::setMakeupGainDb (float db) noexcept
{
    makeupGainDb  = std::clamp (db, -100.0f, 40.0f);
    targetMakeupGain = std::pow (10.0f, makeupGainDb / 20.0f);
}

void LA2ACompressor::updateSidechain (float sample) noexcept
{
    // 侧链三段双二阶级联：必须作用在【有符号】信号上再整流
    // 先整流会让低架滤波变成全频段 +3.5 dB 提升（踩过这个坑）
    if (scEqEnabled)
        for (auto& bq : scEq)
            sample = bq.process (sample);

    const float det = std::fabs (sample);

    // ── 2) 侧链驱动（Peak Reduction 的增益）─────────────
    const float x = det * linearPeakGain;   // ramp 后的当前值，不再每采样 pow

    // ── 3) attack：真机 EL 面板被强信号瞬间点亮 ──
    // （曾经的"跳跃加速"是死代码：clamp(sqrt(jump),1,1) 恒为 1，已移除）
    const float p = x * x;
    const float pDet = p;   // 直接跟瞬时功率（加平均会引入 3 dB 电平偏差）
    const float brightPrev = kAttackMix * brightFast + (1.0f - kAttackMix) * brightSlow;

    // 记忆深度 → 释放速度：CdS 压得越深，恢复越慢
    // 用 r3/bright 的【比例】而不是绝对 dB（绝对 dB 跨度太大，缩放会失效）
    const float memRatio = (brightPrev > 1e-9f) ? (r1 / brightPrev) : 0.0f;
    const float memN  = std::clamp (memRatio * 2.5f, 0.0f, 1.0f);

    // 释放 = 双曲律 GR(t)=A/(t+t0)，A=1126 dB·ms，t0=121 ms（单指数残差是它的 4.7 倍）
    // 等价 τ = A/GR：压得越深恢复越快，越接近恢复完越慢 —— 指数衰减做不出这个形状
    // 判据用【实际施加的】GR（闭环后约是开环的 1/4.8），用开环会让尾巴掉太快
    const float grDepth = std::max (0.0f,
                                    -20.0f * std::log10 (std::max (currentGain, 1.0e-6f)));
    const float aMs   = kRelQuadA * (kRelQuadAMin + (1.0f - kRelQuadAMin) * memN);
    const float tauMs = (grDepth > 0.2f)
                          ? std::clamp (aMs / std::pow (grDepth, kRelTauExp) + kRelTauFloorMs, kRelTauMinMs, kRelTauMaxMs)
                          : kRelTauMaxMs;
    const float cRelNow = coefficientFor (tauMs);

    const float cbF = (pDet > brightFast) ? cAttackFast : cRelNow;
    brightFast += (pDet - brightFast) * cbF;

    const float cbS = (pDet > brightSlow) ? cAttackSlow : cRelNow;
    brightSlow += (pDet - brightSlow) * cbS;

    const float bright = kAttackMix * brightFast + (1.0f - kAttackMix) * brightSlow;

    // 记忆状态：慢充慢放，表示"之前累积了多少光量"
    // 三段并联释放试过会失控（GR 冲到 39 dB），现用单段释放 + 独立记忆调制
    r1 += (bright - r1) * ((bright > r1) ? cMemRise : cMemFall);

    // ── 5) 电平：单段（余辉不再参与求和）──────────────────
    const float cds = bright;


    const float lvlDb  = 10.0f * std::log10 (cds + 1e-12f);   // = 20log10(|x|) 的等效

    // ── 6) 静态 GR 曲线：软拐点 + 线性斜率 ──────────────
    const float over  = lvlDb - kKneeDb;

    // 两段斜率：单一线性斜率拟合不出"低电平偏深、高电平偏浅"的凸曲线
    // kSlope 管中低电平；kSlope2 只在 over 超过 kKnee2Db 后生效
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

    // 参数平滑：本块内逐采样线性 ramp 到目标值。
    // 稳态时 target == 当前值 → 步长恰好 0 → 输出与"完全不平滑"时逐采样相同。
    const float stepPeakGain   = (targetPeakGain   - linearPeakGain) / (float) numSamples;
    const float stepMakeupGain = (targetMakeupGain - makeupGain)     / (float) numSamples;
    const float invN = 1.0f / kSatN;

    for (int n = 0; n < numSamples; ++n)
    {
        // NaN / Inf 防护：dcState 会自己乘 0.995 反馈回来，一旦进 NaN 就永久锁死，
        // 只能重新 prepare 才能恢复（表现为整机静音或数字噪声）。
        if (! std::isfinite (feedback))
        {
            feedback   = 0.0f;
            brightFast = brightSlow = r1 = 0.0f;
            dcState    = dcPrev = 0.0f;
            currentGain = 1.0f;
        }

        linearPeakGain += stepPeakGain;
        makeupGain     += stepMakeupGain;

        // 侧链读【上一采样点的增益衰减后信号】，且【不含 makeup】——
        // 真机的侧链接在 Gain 旋钮之前，所以 Gain 不影响压缩量（手册原文）
        updateSidechain (feedback);

        float nextFeedback = 0.0f;

        for (int ch = 0; ch < numChannels; ++ch)
        {
            const float in   = channels[ch][n];
            const float gain = in * currentGain;          // 增益衰减后（侧链取这个）
            float out        = gain * makeupGain;         // 带 makeup

            // NaN / Inf 防护：非法输入当成 0，并清掉直流阻断器状态
            if (! std::isfinite (out))
            {
                out = 0.0f;
                dcState = dcPrev = 0.0f;
            }

            // 输出级软饱和 + 偶次谐波项（拟合 UAD 的 H2-H7）：T=2.052441  n=30
            // a2=+0.01234（二次）a4=-0.008347（四次）；实测 0 dBFS 时基波损失 8.3 dB
            //
            // ⚠️ 不能直接算 pow(1 + au^n, 1/n)：au > 19.28 时 au^n 在 float32 里溢出成 Inf
            //    → 结果被算成 0，整个半周期被"挖成平底"（Gain 拧到 78 以上就能听到）。
            //    这里用等价形式：|u| ≥ 1 时把 au^n 提出来，改算 (1/au)^n（≤1，永不溢出）。
            const float u  = out / kSatT;
            const float au = std::fabs (u);
            const float denom = (au <= 1.0f)
                                  ? std::pow (1.0f + std::pow (au, kSatN), invN)
                                  : au * std::pow (1.0f + std::pow (1.0f / au, kSatN), invN);
            const float y  = (denom > 0.0f) ? (out / denom) : 0.0f;
            const float y2 = y * y;
            const float y3 = y2 * y;
            const float y4 = y2 * y2;
            const float y5 = y4 * y;
            const float sat = y + kSatA2 * y2 + kSatA3 * y3 + kSatA4 * y4 + kSatA5 * y5;

            // 一阶直流阻断：不对称项会引入直流
            dcState = sat - dcPrev + dcCoef * dcState;
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

    // 吸附到目标值：消除 ramp 的浮点累积误差，保证稳态逐采样完全一致
    linearPeakGain = targetPeakGain;
    makeupGain     = targetMakeupGain;

    return blockMaxGr;
}

} // namespace toreia2a
