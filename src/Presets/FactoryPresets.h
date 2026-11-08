/*
  ─────────────────────────────────────────────────────────────────────────────
   TOIRE-2A 出厂预设

   命名结构参考 UAD LA-2A 原厂预设（Vocal 1–4 / Light Comp / Bass / Bass Limit /
   Snare Color / Snare Limiter / Roomy），但不使用任何艺术家姓名 ——
   那属于 Universal Audio 的授权内容。

   数值来源：
     Peak  —— 按目标压缩量选定
     Gain  —— 用实测反解，使 (Gain 带来的补偿增益) ≈ 该 Peak 下的平均压缩量，
              即"输出电平大致回到输入电平"。实测基准：一段 21 秒的鼓+贝斯+和弦段落。

   ⚠️ 这些是起点值，不是金科玉律。混音时凭耳朵调。
  ─────────────────────────────────────────────────────────────────────────────
*/

#pragma once

namespace toreia2a
{

struct FactoryPreset
{
    const char* name;
    float peakReduction;   // 0..100
    float makeupGain;      // 0..100
    int   meterMode;       // 0 = GR, 1 = Output +10, 2 = Output +4
};

// 顺序即 Program 号；第 0 个是插件默认状态
inline const FactoryPreset kFactoryPresets[] =
{
    // name            Peak   Gain   meter
    { "Init",            30.0f, 50.0f, 0 },
    { "Light Comp",      25.0f, 23.0f, 0 },
    { "Vocal 1",         35.0f, 28.0f, 0 },
    { "Vocal 2",         45.0f, 32.0f, 0 },
    { "Vocal 3",         55.0f, 37.0f, 0 },
    { "Vocal 4",         65.0f, 46.0f, 0 },
    { "Bass",            40.0f, 30.0f, 0 },
    { "Bass Limit",      60.0f, 39.0f, 0 },
    { "Snare Color",     35.0f, 28.0f, 0 },
    { "Snare Limiter",   55.0f, 37.0f, 0 },
    { "Roomy",           30.0f, 25.0f, 0 },
    { "Mix Bus",         35.0f, 28.0f, 0 },
};

inline constexpr int kNumFactoryPresets =
    (int) (sizeof (kFactoryPresets) / sizeof (kFactoryPresets[0]));

} // namespace toreia2a
