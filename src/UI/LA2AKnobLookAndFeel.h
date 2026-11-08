#pragma once

#include <JuceHeader.h>

namespace toreia2a
{

/**
 * LA-2A 风格旋钮：黑色旋钮 + 白色指针 + 外圈 0–100 刻度。
 *
 * 参照真机面板：GAIN 和 PEAK REDUCTION 都是 0–100 的拨盘，
 * 每 5 一格刻度线，每 20 标数字。
 */
class LA2AKnobLookAndFeel : public juce::LookAndFeel_V4
{
public:
    LA2AKnobLookAndFeel()
    {
        setColour (juce::Slider::textBoxTextColourId,      juce::Colour (0xffd8d8d8));
        setColour (juce::Slider::textBoxOutlineColourId,   juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    }

    void drawRotarySlider (juce::Graphics& g,
                           int x, int y, int width, int height,
                           float sliderPos, float startAngle, float endAngle,
                           juce::Slider&) override
    {
        const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat();
        const auto centre = bounds.getCentre();

        // 半径要留出「外圈弧 + 刻度线 + 数字」的空间，
        // 否则刻度数字会被裁掉：arcR=1.18r，刻度到 +9，数字中心在 +20（高 12）
        const float maxR   = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const float radius = juce::jmax (12.0f, (maxR - 30.0f) / 1.20f);

        // ── 外圈轨道 ─────────────────────────────────────
        const float arcR = radius * 1.18f;
        juce::Path track;
        track.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f,
                             startAngle, endAngle, true);
        g.setColour (juce::Colour (0xff3a3a42));
        g.strokePath (track, juce::PathStrokeType (2.0f,
                      juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // ── 已走过的弧（高亮）───────────────────────────
        const float angle = startAngle + sliderPos * (endAngle - startAngle);
        juce::Path active;
        active.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f,
                              startAngle, angle, true);
        g.setColour (juce::Colour (0xffe0b050));
        g.strokePath (active, juce::PathStrokeType (3.0f,
                      juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // ── 刻度线 + 数字（0–100）───────────────────────
        g.setFont (juce::Font (juce::FontOptions (10.0f)));
        for (int v = 0; v <= 100; v += 5)
        {
            const float t   = static_cast<float> (v) / 100.0f;
            const float a   = startAngle + t * (endAngle - startAngle);
            const bool  maj = (v % 20 == 0);

            const float rIn  = arcR + 3.0f;
            const float rOut = arcR + (maj ? 9.0f : 6.0f);

            juce::Point<float> p1 (centre.x + rIn  * std::sin (a), centre.y - rIn  * std::cos (a));
            juce::Point<float> p2 (centre.x + rOut * std::sin (a), centre.y - rOut * std::cos (a));

            g.setColour (maj ? juce::Colour (0xffb8b8c0) : juce::Colour (0xff63636c));
            g.drawLine ({ p1, p2 }, maj ? 1.4f : 1.0f);

            if (maj)
            {
                const float rTxt = arcR + 20.0f;
                const juce::Point<float> pt (centre.x + rTxt * std::sin (a),
                                             centre.y - rTxt * std::cos (a));
                g.setColour (juce::Colour (0xffb8b8c0));
                g.drawText (juce::String (v),
                            juce::Rectangle<float> (34.0f, 12.0f).withCentre (pt),
                            juce::Justification::centred);
            }
        }

        // ── 旋钮本体 ─────────────────────────────────────
        const auto body = juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (centre);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3a3a40), body.getX(), body.getY(),
                                                 juce::Colour (0xff141418), body.getRight(), body.getBottom(),
                                                 false));
        g.fillEllipse (body);
        g.setColour (juce::Colour (0xff0c0c0f));
        g.drawEllipse (body, 1.5f);

        // 指针
        const float pointerR = radius * 0.92f;
        const juce::Point<float> tip (centre.x + pointerR * std::sin (angle),
                                      centre.y - pointerR * std::cos (angle));
        const juce::Point<float> root (centre.x + radius * 0.22f * std::sin (angle),
                                       centre.y - radius * 0.22f * std::cos (angle));
        g.setColour (juce::Colour (0xfff0f0f0));
        g.drawLine ({ root, tip }, 2.6f);
    }
};

} // namespace toreia2a
