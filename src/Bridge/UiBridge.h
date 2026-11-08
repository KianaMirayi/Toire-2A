#pragma once

#include <atomic>
#include <cstdint>
#include <JuceHeader.h>

namespace toreia2a
{

/**
 * UI 遥测快照。
 *
 * 约定：音频线程只写、UI 线程只读，通过无锁原子量传递。
 * 原生 UI 和未来的 WebView UI 读的是同一份结构 —— 这是"换前端不用改 DSP"的关键。
 */
struct UiTelemetry
{
    float gainReductionDb = 0.0f;    // 增益衰减，负值（dB）
    float inputPeakDb     = -96.0f;  // 输入峰值（dBFS）
    float outputPeakDb    = -96.0f;  // 输出峰值（dBFS）
    float vuDb            = -96.0f;  // 表头值（2A 默认显示 GR 或输出电平）
    std::uint32_t frame   = 0;       // 帧计数，UI 用它判断音频是否在跑
};

/**
 * DSP → UI 的单向桥。
 *
 * 音频线程：publish()      （无分配、无锁、无系统调用）
 * UI  线程：snapshot()     （30 Hz Timer 或 WebView 的 timerCallback 里调用）
 *
 * 未来接 WebView 时，UI 端只需要把 snapshot() 转成 emitEventIfBrowserIsVisible
 * 的载荷 —— 见 toWebPayload()。
 */
class UiBridge
{
public:
    // ── 音频线程 ─────────────────────────────────────────────
    void publish (const UiTelemetry& t) noexcept
    {
        gainReductionDb.store (t.gainReductionDb, std::memory_order_relaxed);
        inputPeakDb.store     (t.inputPeakDb,     std::memory_order_relaxed);
        outputPeakDb.store    (t.outputPeakDb,    std::memory_order_relaxed);
        vuDb.store            (t.vuDb,            std::memory_order_relaxed);
        frame.store           (t.frame,           std::memory_order_release);
    }

    // ── UI 线程 ─────────────────────────────────────────────
    [[nodiscard]] UiTelemetry snapshot() const noexcept
    {
        UiTelemetry t;
        t.frame           = frame.load           (std::memory_order_acquire);
        t.gainReductionDb = gainReductionDb.load (std::memory_order_relaxed);
        t.inputPeakDb     = inputPeakDb.load     (std::memory_order_relaxed);
        t.outputPeakDb    = outputPeakDb.load    (std::memory_order_relaxed);
        t.vuDb            = vuDb.load            (std::memory_order_relaxed);
        return t;
    }

    [[nodiscard]] bool hasNewFrame (std::uint32_t lastFrame) const noexcept
    {
        return frame.load (std::memory_order_acquire) != lastFrame;
    }

    // ── 前端无关的载荷构造 ───────────────────────────────────
    // 原生 UI 可以直接用；WebView UI 直接丢给 emitEventIfBrowserIsVisible。
    static juce::Array<juce::var> toWebPayload (const UiTelemetry& t)
    {
        juce::Array<juce::var> a;
        a.ensureStorageAllocated (4);
        a.add (juce::var (t.gainReductionDb));
        a.add (juce::var (t.inputPeakDb));
        a.add (juce::var (t.outputPeakDb));
        a.add (juce::var (t.vuDb));
        return a;
    }

    /** 线性 → dBFS，-96 dB 以下统一夹到 -96（避免 -inf）。 */
    static float linToDb (float lin) noexcept
    {
        if (lin < 1.58e-5f) return -96.0f;
        return juce::jmax (-96.0f, 20.0f * std::log10 (lin));
    }

private:
    std::atomic<float>         gainReductionDb { 0.0f };
    std::atomic<float>         inputPeakDb     { -96.0f };
    std::atomic<float>         outputPeakDb    { -96.0f };
    std::atomic<float>         vuDb            { -96.0f };
    std::atomic<std::uint32_t> frame           { 0 };
};

} // namespace toreia2a
