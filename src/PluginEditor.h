#pragma once

#include <JuceHeader.h>

#include "Bridge/UiBridge.h"
#include "UI/LA2AKnobLookAndFeel.h"

class TOIRE2AAudioProcessor;

/**
 * 原生 UI（当前阶段）。
 *
 * 这一层故意写得很薄：只从 UiBridge 取快照、只负责画。
 * 以后换 WebView2 前端时，删掉这个类、在 PluginProcessor::createEditor()
 * 里换一行就行，DSP 与 Bridge 一行都不用动。
 */
class TOIRE2AAudioProcessorEditor : public juce::AudioProcessorEditor,
                                    private juce::Timer
{
public:
    explicit TOIRE2AAudioProcessorEditor (TOIRE2AAudioProcessor&);
    ~TOIRE2AAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    TOIRE2AAudioProcessor& processor;

    toreia2a::LA2AKnobLookAndFeel laf;

    juce::Slider peakSlider, gainSlider;
    juce::Label  peakLabel, gainLabel;

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    std::unique_ptr<SliderAttachment> peakAttachment, gainAttachment;

    // ── 从桥读到的遥测（UI 线程本地副本）──
    toreia2a::UiTelemetry telemetry;
    std::uint32_t lastFrame  = 0;
    float         displayGrDb = 0.0f;

    juce::Rectangle<int> meterBounds;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TOIRE2AAudioProcessorEditor)
};
