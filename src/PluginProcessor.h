#pragma once

#include <JuceHeader.h>

#include "Bridge/UiBridge.h"
#include "DSP/LA2ACompressor.h"
#include "Presets/FactoryPresets.h"

class TOIRE2AAudioProcessor : public juce::AudioProcessor,
                              private juce::AudioProcessorValueTreeState::Listener
{
public:
    TOIRE2AAudioProcessor();
    ~TOIRE2AAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "TOiRE-2A"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    // ── 宿主预设（Program API）──────────────────────────────
    // 出厂预设表见 Presets/FactoryPresets.h。宿主（Cubase / Studio Pro / Reaper）
    // 会把它们显示在插件窗口的预设下拉里。
    int getNumPrograms() override { return toreia2a::kNumFactoryPresets; }
    int getCurrentProgram() override { return currentPreset; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    /** 从 XML 恢复状态（用户预设文件用）。
        ⚠️ 必须走这个入口而不是直接 apvts.replaceState() ——
        replaceState 会对每个参数触发 setValueNotifyingHost，进而回调 "preset" 监听器
        调 applyPreset，把刚从文件里恢复的 Peak / Gain 覆写回出厂预设值。 */
    void loadStateXml (const juce::XmlElement& xml);

    /** 应用第 index 个出厂预设（会同时改 Peak / Gain / 表头档位）。
        也用于 PluginEditor 暴露给网页端的预设下拉。 */
    void applyPreset (int index);

    /** 当前预设号（-1 = 已被手动改动，不匹配任何出厂预设） */
    int getCurrentPresetIndex() const noexcept { return currentPreset; }

    juce::AudioProcessorValueTreeState apvts;

    // ── UI 桥：原生 UI 和未来的 WebView UI 都从这里取数据 ──
    [[nodiscard]] const toreia2a::UiBridge& getUiBridge() const noexcept { return uiBridge; }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // 预设参数变化时（网页下拉或宿主 Program 菜单）应用预设
    void parameterChanged (const juce::String& parameterID, float newValue) override;

    // 正在应用预设时，忽略自己触发的参数回调
    bool applyingPreset = false;

    // 正在从宿主 / 文件恢复状态时，同样忽略 "preset" 回调（见 loadStateXml 注释）
    std::atomic<bool> loadingState { false };
    int  currentPreset = 0;

    toreia2a::LA2ACompressor compressor;
    toreia2a::UiBridge       uiBridge;

    std::atomic<float>* peakReductionParam = nullptr;
    std::atomic<float>* makeupGainParam    = nullptr;
    std::atomic<float>* meterAttackParam   = nullptr;
    std::atomic<float>* meterReleaseParam  = nullptr;
    std::atomic<float>* meterModeParam     = nullptr;
    std::atomic<float>* bypassParam        = nullptr;

    std::uint32_t frameCounter = 0;

    // ── 旁路交叉淡化 ──────────────────────────────────────
    // 0 = 完全处理，1 = 完全直通。只在切换期间逐采样 ramp，
    // 完全未旁路时（常态）不拷干声、不做混合 → 输出与硬旁路版本逐采样相同。
    juce::AudioBuffer<float> dryScratch;
    float bypassAmount = 0.0f;

    // 表头弹道 = 二阶 VU 积分器（不是块内峰值，否则指针跟着每个尖峰乱跳）
    // τ=62ms → 99% 上升约 350ms；τ=110ms → 50% 回落约 185ms
    static constexpr float kMeterAttackSec  = 0.062f;
    static constexpr float kMeterReleaseSec = 0.110f;
    float meterS1 = 0.0f;      // 第一级
    float meterS2 = 0.0f;      // 第二级（输出）
    float meterPeakHold = -96.0f;   // 表头模式 2/3 用的输出峰值（慢释放）

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TOIRE2AAudioProcessor)
};

/** ⚠️ 必须是裸函数（默认 __cdecl），不要加 JUCE_CALLTYPE */
juce::AudioProcessor* createPluginFilter();
