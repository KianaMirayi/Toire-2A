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

// WebView2 版编辑器（CMake 开 TOIRE2A_WEBVIEW2 时由 createEditor() 选用）
#if TOREI2A_HAS_WEBVIEW

/** WebBrowserComponent 默认不通知"页面加载完成"，这里暴露出来。 */
class Torei2aWebView : public juce::WebBrowserComponent
{
public:
    using WebBrowserComponent::WebBrowserComponent;

    std::function<void (const juce::String&)> onPageLoaded;

    void pageFinishedLoading (const juce::String& url) override
    {
        if (onPageLoaded)
            onPageLoaded (url);
    }
};

/**
 * WebView2 前端编辑器。
 *
 * 数据流：
 *   APVTS ←→ WebSliderRelay/WebSliderParameterAttachment ←→ JS 旋钮（双向、可自动化）
 *   UiBridge ──snapshot()──► 30 Hz Timer ──emitEventIfBrowserIsVisible("telemetry")──► JS 表头
 *
 * 注意（踩过的坑，别改）：
 *   · setBounds() 必须在 addAndMakeVisible() 之前
 *   · 首次可见（visibilityChanged）才创建 WebView，避免 DAW 首载崩溃
 *   · WebView2 userData 放 %APPDATA%，不放 %TEMP%
 *   · pageFinishedLoading 之前一律不 emit
 *   · C++→JS 只用 emitEventIfBrowserIsVisible，绝不用 evaluateJavascript
 */
class TOIRE2AWebEditor : public juce::AudioProcessorEditor,
                         private juce::Timer
{
public:
    explicit TOIRE2AWebEditor (TOIRE2AAudioProcessor&);
    ~TOIRE2AWebEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void visibilityChanged() override;

private:
    void timerCallback() override;
    void ensureWebView();

    // ── 用户预设（存 / 读 .toire2a 文件）──────────────────────
    void handlePresetAction (const juce::Array<juce::var>& args);
    void savePresetAsDialog();
    void loadPresetDialog();
    void notifyPresetName (const juce::String& name, bool isUser);
    static juce::File presetDirectory();

    /** 预设文件对话框（异步，不阻塞音频线程）*/
    std::unique_ptr<juce::FileChooser> presetChooser;

    TOIRE2AAudioProcessor& processor;

    std::unique_ptr<Torei2aWebView> webView;

    // 参数双向绑定（名称必须与 JS 侧 getSliderState 的名字一致）
    juce::WebSliderRelay peakReductionRelay { "peakReduction" };
    juce::WebSliderRelay makeupGainRelay    { "makeupGain" };
    juce::WebSliderRelay meterAttackRelay   { "meterAttack" };
    juce::WebSliderRelay meterReleaseRelay  { "meterRelease" };
    juce::WebSliderRelay bypassRelay        { "bypass" };
    juce::WebSliderRelay presetRelay        { "preset" };

    // 表头模式（AudioParameterChoice 0/1/2）：用 slider relay 走，
    // JS 侧读归一化值 0 / 0.5 / 1，写回时同样用 setNormalisedValue 切档。
    juce::WebSliderRelay meterModeRelay     { "meterMode" };

    std::unique_ptr<juce::WebSliderParameterAttachment> peakReductionAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> makeupGainAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> meterAttackAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> meterReleaseAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> bypassAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> presetAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> meterModeAttachment;

    // 遥测载荷 [grDb, inPeakDb, outPeakDb, vuDb, frame]
    juce::Array<juce::var> telemetryPayload;

    bool pageLoaded = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TOIRE2AWebEditor)
};

#endif // TOREI2A_HAS_WEBVIEW
