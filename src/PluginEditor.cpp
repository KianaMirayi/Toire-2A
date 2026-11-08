#include "PluginEditor.h"

#include "PluginProcessor.h"

TOIRE2AAudioProcessorEditor::TOIRE2AAudioProcessorEditor (TOIRE2AAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    setOpaque (true);
    setSize (760, 420);

    setLookAndFeel (&laf);

    auto setupKnob = [this] (juce::Slider& s, juce::Label& l, const juce::String& text)
    {
        s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 20);
        s.setNumDecimalPlacesToDisplay (0);
        s.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f,
                               juce::MathConstants<float>::pi * 2.75f,
                               true);
        s.setColour (juce::Slider::rotarySliderFillColourId,    juce::Colour (0xffe0b050));
        s.setColour (juce::Slider::rotarySliderOutlineColourId, juce::Colour (0xff3a3a42));
        s.setColour (juce::Slider::thumbColourId,               juce::Colour (0xffe8e8e8));
        s.setColour (juce::Slider::textBoxTextColourId,         juce::Colour (0xffd8d8d8));
        s.setColour (juce::Slider::textBoxOutlineColourId,      juce::Colours::transparentBlack);
        addAndMakeVisible (s);

        l.setText (text, juce::dontSendNotification);
        l.setJustificationType (juce::Justification::centred);
        l.setColour (juce::Label::textColourId, juce::Colour (0xff9a9aa4));
        l.setFont (juce::Font (juce::FontOptions (12.0f)));
        addAndMakeVisible (l);
    };

    setupKnob (peakSlider, peakLabel, "PEAK REDUCTION");
    setupKnob (gainSlider, gainLabel, "GAIN");

    peakAttachment = std::make_unique<SliderAttachment> (processor.apvts, "peakReduction", peakSlider);
    gainAttachment = std::make_unique<SliderAttachment> (processor.apvts, "makeupGain",    gainSlider);

    // 30 Hz 从桥里取数据（与未来 WebView 的 timerCallback 频率一致）
    startTimerHz (30);
}

TOIRE2AAudioProcessorEditor::~TOIRE2AAudioProcessorEditor()
{
    stopTimer();
    peakAttachment.reset();
    gainAttachment.reset();
    setLookAndFeel (nullptr);
}

void TOIRE2AAudioProcessorEditor::timerCallback()
{
    const auto t = processor.getUiBridge().snapshot();

    if (t.frame != lastFrame)
    {
        lastFrame   = t.frame;
        displayGrDb = juce::jmin (0.0f, t.gainReductionDb);
    }
    else
    {
        // 音频停了（DAW 暂停 / 旁路）——平滑回零，避免表头冻住
        displayGrDb *= 0.85f;
        if (displayGrDb > -0.05f)
            displayGrDb = 0.0f;
    }

    repaint (meterBounds);
}

void TOIRE2AAudioProcessorEditor::resized()
{
    auto r = getLocalBounds().reduced (16);
    r.removeFromTop (34);      // 标题条
    r.removeFromBottom (18);   // 底部说明条

    meterBounds = r.removeFromRight (200).reduced (10, 8);

    const int knobW = r.getWidth() / 2;
    auto placeKnob = [] (juce::Rectangle<int> cell, juce::Slider& s, juce::Label& l)
    {
        l.setBounds (cell.removeFromBottom (22));
        const int side = juce::jmin (cell.getWidth(), cell.getHeight() - 26);
        auto box = juce::Rectangle<int> (side, side + 26).withCentre (cell.getCentre());
        s.setBounds (box);
    };

    // 左 GAIN，右 PEAK REDUCTION（与真机面板顺序一致）
    auto left = r.removeFromLeft (knobW);
    placeKnob (left, gainSlider, gainLabel);

    auto right = r;
    placeKnob (right, peakSlider, peakLabel);
}

void TOIRE2AAudioProcessorEditor::paint (juce::Graphics& g)
{
    // 背景（与未来 Web UI 的 body 背景保持一致，切换时不会闪）
    g.fillAll (juce::Colour (0xff1a1a1e));

    // 标题
    g.setColour (juce::Colour (0xffe0b050));
    g.setFont (juce::Font (juce::FontOptions (20.0f).withStyle ("Bold")));
    g.drawText ("TOiRE-2A", getLocalBounds().reduced (16).removeFromTop (26),
                juce::Justification::centredLeft);

    // ── 增益衰减表 ────────────────────────────────────────
    if (! meterBounds.isEmpty())
    {
        auto outer = meterBounds;

        g.setColour (juce::Colour (0xff2a2a30));
        g.fillRoundedRectangle (outer.toFloat(), 4.0f);
        g.setColour (juce::Colour (0xff3a3a42));
        g.drawRoundedRectangle (outer.toFloat(), 4.0f, 1.0f);

        auto box = outer.reduced (8, 6);

        // 表头标题
        auto titleArea = box.removeFromTop (15);
        g.setColour (juce::Colour (0xff9a9aa4));
        g.setFont (juce::Font (juce::FontOptions (11.0f)));
        g.drawText ("GAIN REDUCTION", titleArea, juce::Justification::centred);

        // 数值读数
        auto readout = box.removeFromTop (26);
        g.setColour (juce::Colour (0xffe0b050));
        g.setFont (juce::Font (juce::FontOptions (19.0f).withStyle ("Bold")));
        const juce::String txt = (displayGrDb < -0.05f)
                                   ? juce::String (displayGrDb, 1) + " dB"
                                   : juce::String ("0.0 dB");
        g.drawText (txt, readout, juce::Justification::centred);

        // 刻度 + 柱体（右侧 22 px 留给刻度）
        auto ticks = box.removeFromRight (22);
        auto inner = box.reduced (4, 2);

        const float kMaxGr = 20.0f;
        const float frac   = juce::jlimit (0.0f, 1.0f, -displayGrDb / kMaxGr);

        // 柱体从下往上长
        const int barH = juce::roundToInt (inner.getHeight() * frac);
        if (barH > 0)
        {
            auto bar = inner.removeFromBottom (barH);
            g.setColour (juce::Colour (0xffe0b050));
            g.fillRoundedRectangle (bar.toFloat(), 2.0f);
        }

        // 刻度线 + 标签（0 / −5 / −10 / −20）
        g.setFont (juce::Font (juce::FontOptions (8.5f)));
        for (float db : { 0.0f, -5.0f, -10.0f, -15.0f, -20.0f })
        {
            const float f = juce::jlimit (0.0f, 1.0f, -db / kMaxGr);
            const int y = inner.getBottom() - juce::roundToInt (inner.getHeight() * f);
            g.setColour (juce::Colour (0xff55555c));
            g.drawHorizontalLine (y, (float) inner.getX() - 3.0f, (float) inner.getRight());
            g.setColour (juce::Colour (0xff8a8a94));
            g.drawText (juce::String ((int) db),
                        juce::Rectangle<int> (ticks.getX(), y - 5, ticks.getWidth(), 10),
                        juce::Justification::centredLeft);
        }
    }

    // 底部说明
    g.setColour (juce::Colour (0xff5a5a64));
    g.setFont (juce::Font (juce::FontOptions (10.0f)));
    g.drawText ("optical leveling amplifier  /  prototype 0.1",
                getLocalBounds().reduced (16).removeFromBottom (16),
                juce::Justification::centredRight);
}
