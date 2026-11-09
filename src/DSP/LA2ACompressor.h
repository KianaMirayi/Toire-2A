#pragma once

#include <algorithm>
#include <array>
#include <cmath>

namespace toreia2a
{

/**
 * LA-2A 风格光电压缩器 —— 反馈式，常数由实测校准。
 *
 * 结构（对应 docs/LA-2A-implementation-notes.md 与 docs/R37-侧链测量.md）：
 *
 *   输入 ──→ Σ ──→ 输出
 *            ↑
 *           分压(光敏电阻)
 *            ↑
 *        EL 亮度 ← 侧链驱动 ← 输出反馈（手册 Section 16：feed-back style）
 *
 * 常数来源：
 *   attack  ≈ 11 ms    —— UAD 实测，GR 到 63% 的时间
 *   release 快段       —— UAD 实测，20 ms 过载 50% 恢复 ≈ 35 ms
 *   慢尾 τ  ≈ 1100 ms  —— UAD 实测，500 ms 过载 400→3200 ms 区间拟合
 *   makeup  +14.5 dB   —— UAD 实测，Gain 拨盘 50、线性区
 *   拐点 ≈ −27 dBFS    —— UAD 实测，Peak 拨盘 50 时的压缩起点
 *   GR@0dBFS = 17.1 dB —— UAD 实测
 */
class LA2ACompressor
{
public:
    void prepare (double sampleRate) noexcept;
    void reset() noexcept;

    void setPeakReduction (float normalised01) noexcept;   // 0..1
    void setMakeupGainDb (float db) noexcept;

    /** 侧链频率响应（实测拟合）开关，默认开。关掉可做 A/B 对比。 */
    void setSidechainEqEnabled (bool on) noexcept { scEqEnabled = on; }

    /** 处理整块音频（原地）。返回本块最大增益衰减（dB，负值）。 */
    float process (float* const* channels, int numChannels, int numSamples) noexcept;

    // ── 表头 ──────────────────────────────────────────────
    [[nodiscard]] float getLastGainReductionDb() const noexcept { return lastGrDb; }
    [[nodiscard]] float getInputPeak() const noexcept  { return inPeak; }
    [[nodiscard]] float getOutputPeak() const noexcept { return outPeak; }

    // ── 实测校准常数 ──────────────────────────────────────
    // 实测 attack 是两段：EL 面板几乎瞬时（~0.4 ms），CdS 慢速上升（~15 ms）
    static constexpr float kAttackFastMs  = 3.0f;
    static constexpr float kAttackSlowMs  = 45.0f;
    static constexpr float kAttackMix     = 0.60f;     // 快段权重
    

    // 释放双曲律 GR(t)=A/(t+t0)，等效 τ = A/GR（UAD 实测 A=1126, t0=121）
    // kRelQuadA = A 的基准；kRelQuadAMin = 无记忆时 A 的最小倍率
    static constexpr float kRelQuadA    = 11000.0f;   // 1/GR 项系数（实测 A 在 900–2300 之间随 GR 上升）
    static constexpr float kRelQuadAMin = 0.40f;
    static constexpr float kRelTauMaxMs = 4000.0f;   // τ 上限
    // τ 下限：实测 UAD 在高 GR 时 τ 稳定在 ~125 ms（不随 GR 继续变小）
    static constexpr float kRelTauMinMs = 30.0f;
    // 实测 UAD：τ·GR 随 GR 上升（900→2300），说明规律是 τ = A/GR + 地板
    static constexpr float kRelTauFloorMs = 45.0f;
    // τ 对 GR 的指数：>1 会让"越接近恢复完越慢"的曲率更强
    // 实测 UAD 的 t80/t50 ≈ 5.3，指数 1（纯 1/GR）只能做出 3.2
    static constexpr float kRelTauExp = 1.80f;

    // 记忆状态：慢充慢放，只用于调制 A（不参与电平）
    static constexpr float kMemRiseMs     = 600.0f;
    static constexpr float kMemFallMs     = 4000.0f;

    static constexpr float kPeakGainMaxDb = 30.0f;

    // 输出级饱和：y = T·u/(1+|u|^n)^(1/n) + a·y²,  u = x/T
    // n=8.5 接近【硬限幅】而非软 tanh —— 这是"少压缩多失真"的来源
    static constexpr float kSatT  = 2.052441f;   // 拐点 +6.2 dBFS
    static constexpr float kSatN  = 30.0f;       // 硬度（接近硬限幅）
    static constexpr float kSatA2 = 0.012340f;   // 二次谐波
    static constexpr float kSatA3 = 0.003979f;   // 三次谐波
    static constexpr float kSatA4 = -0.008347f;  // 四次谐波
    static constexpr float kSatA5 = -0.017387f;  // 五次谐波
    static constexpr float kKneeDb        = -7.37f;   // 侧链内部主拐点（makeup 之前取信号，已下移 14.5 dB）
    static constexpr float kSlope         = 3.75f;      // 主斜率（中低电平）
    static constexpr float kSlope2        = 2.0f;      // 附加斜率（高电平）
    static constexpr float kKnee2Db       = 10.0f;     // 第二拐点（相对主拐点的 over 值）
    static constexpr float kSoft2Db       = 4.0f;      // 第二拐点柔和度
    static constexpr float kSoftDb        = 2.0f;      // 拐点柔和度

private:
    /** 双二阶（Direct Form II Transposed，a0 已归一化） */
    struct Biquad
    {
        float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
        float z1 = 0.0f, z2 = 0.0f;

        void reset() noexcept { z1 = z2 = 0.0f; }

        float process (float x) noexcept
        {
            const float y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
    };

    [[nodiscard]] float coefficientFor (float ms) const noexcept;
    [[nodiscard]] static float softPlus (float x, float soft) noexcept;
    void updateSidechain (float outputSample) noexcept;

    double sampleRate = 48000.0;

    float peakReduction = 0.3f;
    float peakGainDb    = 0.0f;
    float makeupGainDb  = 0.0f;
    float makeupGain    = 1.0f;

    // 侧链频率响应：实测 UAD 的 H(f) 拟合出的三段双二阶级联
    Biquad scEq[3];
    bool   scEqEnabled = true;

    float brightFast = 0.0f;  // EL 亮度快段
    float brightSlow = 0.0f;  // CdS 慢速上升段
    float pEnv      = 0.0f;   // 5 ms 平滑的信号包络（释放判据用）
    float cEnv      = 0.0f;
    float r1        = 0.0f;   // 快释放状态
        float cMemRise = 0.0f, cMemFall = 0.0f;
    float dcState = 0.0f, dcPrev = 0.0f;   // 直流阻断（不对称饱和会引入直流）
    float currentGain = 1.0f;

    // 平滑系数
    float cAttackFast = 0.0f, cAttackSlow = 0.0f, cReleaseFast = 0.0f;
    

    float lastGrDb = 0.0f;

    // 表头峰值（每块重置）
    float inPeak  = 0.0f;
    float outPeak = 0.0f;
};

} // namespace toreia2a
