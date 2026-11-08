#pragma once

#include <JuceHeader.h>

#include "Bridge/UiBridge.h"
#include "DSP/LA2ACompressor.h"

class TOIRE2AAudioProcessor : public juce::AudioProcessor
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

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

    // ── UI 桥：原生 UI 和未来的 WebView UI 都从这里取数据 ──
    [[nodiscard]] const toreia2a::UiBridge& getUiBridge() const noexcept { return uiBridge; }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    toreia2a::LA2ACompressor compressor;
    toreia2a::UiBridge       uiBridge;

    std::atomic<float>* peakReductionParam = nullptr;
    std::atomic<float>* makeupGainParam    = nullptr;

    std::uint32_t frameCounter = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TOIRE2AAudioProcessor)
};

/** ⚠️ 必须是裸函数（默认 __cdecl），不要加 JUCE_CALLTYPE */
juce::AudioProcessor* createPluginFilter();
