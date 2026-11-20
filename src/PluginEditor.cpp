#include "PluginEditor.h"

#include "PluginProcessor.h"

TOIRE2AAudioProcessorEditor::TOIRE2AAudioProcessorEditor (TOIRE2AAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    setOpaque (true);
    setSize (1188, 329);      // 与 UAD LA-2A 面板同比例

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
    // 背景（与 Web UI 的 body / 面板底色一致，切换时不会闪白）
    g.fillAll (juce::Colour (0xff0b0b0c));

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

// ============================================================================
//  WebView2 版编辑器
// ============================================================================
#if TOREI2A_HAS_WEBVIEW

namespace
{
    /**
     * 找 index.html。
     *
     * ⚠️ 必须用 invokedExecutableFile（不是 currentExecutableFile）：
     *    宿主/standalone 的实际路径不一定是 exe 自己所在目录。
     *
     * 目录布局：
     *   Standalone/TOIRE-2A.exe + Standalone/webui/index.html
     *   TOIRE-2A.vst3/Contents/x86_64-win/TOIRE-2A.vst3 + Contents/webui/index.html
     */
    juce::File findWebUiIndex()
    {
        const auto exeDir = juce::File::getSpecialLocation (juce::File::invokedExecutableFile)
                                .getParentDirectory();

        auto dir = exeDir;

        for (int i = 0; i < 8; ++i)
        {
            for (auto* sub : { "webui/index.html",
                               "Contents/webui/index.html",
                               "../webui/index.html",
                               "../../webui/index.html" })
            {
                const auto f = dir.getChildFile (sub);

                if (f.existsAsFile())
                    return f;
            }

            dir = dir.getParentDirectory();
        }

        // 开发期回退：源码树里的 WebUI/（install 到别处时这步自然落空）
        const auto srcPath = juce::String (__FILE__);
        auto srcFile = juce::File (srcPath);

        if (! juce::File::isAbsolutePath (srcPath))
            srcFile = juce::File::getCurrentWorkingDirectory().getChildFile (srcPath);

        const auto devFile = srcFile.getParentDirectory().getParentDirectory()
                                 .getChildFile ("WebUI").getChildFile ("index.html");

        if (devFile.existsAsFile())
            return devFile;

        return {};
    }
} // namespace

TOIRE2AWebEditor::TOIRE2AWebEditor (TOIRE2AAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    setOpaque (true);
    setResizable (false, false);
    setSize (1188, 329);      // 与 UAD LA-2A 面板同比例

    // 参数双向绑定：DAW 自动化 ←→ 网页旋钮
    if (auto* peakParam = processor.apvts.getParameter ("peakReduction"))
        peakReductionAttachment = std::make_unique<juce::WebSliderParameterAttachment> (
            *peakParam, peakReductionRelay, nullptr);

    if (auto* gainParam = processor.apvts.getParameter ("makeupGain"))
        makeupGainAttachment = std::make_unique<juce::WebSliderParameterAttachment> (
            *gainParam, makeupGainRelay, nullptr);

    // 表头弹道（现场可调）
    if (auto* atkParam = processor.apvts.getParameter ("meterAttack"))
        meterAttackAttachment = std::make_unique<juce::WebSliderParameterAttachment> (
            *atkParam, meterAttackRelay, nullptr);

    if (auto* relParam = processor.apvts.getParameter ("meterRelease"))
        meterReleaseAttachment = std::make_unique<juce::WebSliderParameterAttachment> (
            *relParam, meterReleaseRelay, nullptr);

    // 3 位表头开关（AudioParameterChoice）也走 slider relay：
    // 归一化值 0 / 0.5 / 1 对应 Gain Reduction / Output +10 / Output +4。
    if (auto* modeParam = processor.apvts.getParameter ("meterMode"))
        meterModeAttachment = std::make_unique<juce::WebSliderParameterAttachment> (
            *modeParam, meterModeRelay, nullptr);

    // 旁路（面板上的 POWER 开关 → AudioParameterBool，归一化 0 / 1）
    if (auto* bypParam = processor.apvts.getParameter ("bypass"))
        bypassAttachment = std::make_unique<juce::WebSliderParameterAttachment> (
            *bypParam, bypassRelay, nullptr);

    // 预设号（顶部预设条）。参数本身不可自动化，但需要 relay 才能让网页写它。
    if (auto* presetParam = processor.apvts.getParameter ("preset"))
        presetAttachment = std::make_unique<juce::WebSliderParameterAttachment> (
            *presetParam, presetRelay, nullptr);

    // 载荷预分配：[grDb, inPeakDb, outPeakDb, vuDb, frame]
    telemetryPayload.ensureStorageAllocated (5);

    for (int i = 0; i < 5; ++i)
        telemetryPayload.add (juce::var (0.0));

    startTimerHz (30);
}

TOIRE2AWebEditor::~TOIRE2AWebEditor()
{
    stopTimer();

    if (webView != nullptr)
    {
        webView->onPageLoaded = nullptr;

        // 先卸掉页面再析构 WebView2：宿主退出时直接把活页面拆掉容易踩堆损坏
        webView->goToURL ("about:blank");
    }

    webView.reset();

    // attachment 引用 relay（成员），必须先于 relay 析构
    peakReductionAttachment.reset();
    makeupGainAttachment.reset();
    meterAttackAttachment.reset();
    meterReleaseAttachment.reset();
    bypassAttachment.reset();
    presetAttachment.reset();
    meterModeAttachment.reset();
}

void TOIRE2AWebEditor::visibilityChanged()
{
    // ⚠️ 延迟创建：构造期宿主消息循环还不稳，WebView2 环境也没就绪
    if (isVisible())
        ensureWebView();

    AudioProcessorEditor::visibilityChanged();
}

void TOIRE2AWebEditor::ensureWebView()
{
    if (webView != nullptr)
        return;

    auto winOpts = juce::WebBrowserComponent::Options::WinWebView2{};

    // ⚠️ userData 放 %APPDATA%，不要 %TEMP%（清 Temp 会让 WebView2 重新冷启动）
    const auto userData = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                              .getChildFile ("TOIRE-2A")
                              .getChildFile ("WebView2");
    userData.createDirectory();

    winOpts = winOpts.withUserDataFolder (userData)
                     .withStatusBarDisabled()
                     .withBuiltInErrorPageDisabled()
                     .withBackgroundColour (juce::Colour (0xff0b0b0c));

    const auto opts = juce::WebBrowserComponent::Options{}
                          .withBackend (juce::WebBrowserComponent::Options::Backend::webview2)
                          .withWinWebView2Options (winOpts)
                          .withKeepPageLoadedWhenBrowserIsHidden()
                          .withOptionsFrom (peakReductionRelay)
                          .withOptionsFrom (makeupGainRelay)
                          .withOptionsFrom (meterAttackRelay)
                          .withOptionsFrom (meterReleaseRelay)
                          .withOptionsFrom (meterModeRelay)
                          .withOptionsFrom (bypassRelay)
                          .withOptionsFrom (presetRelay)

                          // JS → C++：用户预设的存 / 读。
                          // 只收到一个事件 id，payload 里带 { action: "save" | "load" }。
                          .withEventListener ("presetAction",
                              [this] (const juce::Array<juce::var>& args)
                              {
                                  handlePresetAction (args);
                              });

    webView = std::make_unique<Torei2aWebView> (opts);

    // 不抢键盘焦点：WebView2 一旦获得焦点，DAW 的快捷键会全部失效
    // UI 没有文本输入，关掉点击聚焦即可
    webView->setWantsKeyboardFocus (false);
    webView->setMouseClickGrabsKeyboardFocus (false);

    webView->onPageLoaded = [this] (const juce::String& url)
    {
        // 初始 about:blank 也会回调 —— 别把它当成"页面好了"
        if (url.startsWithIgnoreCase ("about:"))
            return;

        pageLoaded = true;
    };

    // ⚠️⚠️ 顺序是硬性的：setBounds 必须在 addAndMakeVisible 之前，否则 WebView2 永远黑屏
    webView->setBounds (getLocalBounds());
    webView->setOpaque (true);
    addAndMakeVisible (*webView);
    webView->setVisible (true);
    webView->toFront (true);

    const auto html = findWebUiIndex();

    if (html.existsAsFile())
        webView->goToURL (juce::URL (html).toString (false));
    else
        webView->goToURL ("about:blank");
}

void TOIRE2AWebEditor::timerCallback()
{
    if (webView == nullptr)
    {
        ensureWebView();

        if (webView == nullptr)
            return;
    }

    // 页面没加载完之前不推：事件会静默丢掉，且 JS 还没注册监听
    if (! pageLoaded)
        return;

    const auto t = processor.getUiBridge().snapshot();

    telemetryPayload.set (0, juce::var ((double) t.gainReductionDb));
    telemetryPayload.set (1, juce::var ((double) t.inputPeakDb));
    telemetryPayload.set (2, juce::var ((double) t.outputPeakDb));
    telemetryPayload.set (3, juce::var ((double) t.vuDb));
    telemetryPayload.set (4, juce::var ((double) t.frame));

    // ⚠️ 只用 emitEventIfBrowserIsVisible（evaluateJavascript 在 DAW 里静默失败）
    webView->emitEventIfBrowserIsVisible ("telemetry", telemetryPayload);
}

void TOIRE2AWebEditor::resized()
{
    if (webView != nullptr)
        webView->setBounds (getLocalBounds());
}

void TOIRE2AWebEditor::paint (juce::Graphics& g)
{
    // 与 HTML body / 面板底色一致，避免加载期间闪白
    g.fillAll (juce::Colour (0xff0b0b0c));
}

//==============================================================================
//  用户预设：存 / 读 .toire2a 文件
//==============================================================================
juce::File TOIRE2AWebEditor::presetDirectory()
{
    auto dir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("TOiRE-2A")
                   .getChildFile ("presets");
    if (! dir.isDirectory())
        dir.createDirectory();
    return dir;
}

void TOIRE2AWebEditor::notifyPresetName (const juce::String& name, bool isUser)
{
    if (webView == nullptr)
        return;

    juce::Array<juce::var> payload;
    payload.add (name);
    payload.add (isUser ? 1 : 0);
    webView->emitEventIfBrowserIsVisible ("presetLoaded", payload);
}

void TOIRE2AWebEditor::savePresetAsDialog()
{
    auto suggested = presetDirectory().getChildFile ("My Preset.toire2a");

    presetChooser = std::make_unique<juce::FileChooser> (
        "保存用户预设", suggested, "*.toire2a");

    presetChooser->launchAsync (
        juce::FileBrowserComponent::saveMode
            | juce::FileBrowserComponent::canSelectFiles
            | juce::FileBrowserComponent::warnAboutOverwriting,
        [this] (const juce::FileChooser& fc)
        {
            auto f = fc.getResult();
            if (f == juce::File{})
                return;

            if (! f.hasFileExtension ("toire2a"))
                f = f.withFileExtension ("toire2a");

            if (auto xml = processor.apvts.copyState().createXml())
                xml->writeTo (f);

            notifyPresetName (f.getFileNameWithoutExtension(), true);
        });
}

void TOIRE2AWebEditor::loadPresetDialog()
{
    presetChooser = std::make_unique<juce::FileChooser> (
        "载入用户预设", presetDirectory(), "*.toire2a");

    presetChooser->launchAsync (
        juce::FileBrowserComponent::openMode
            | juce::FileBrowserComponent::canSelectFiles,
        [this] (const juce::FileChooser& fc)
        {
            auto f = fc.getResult();
            if (! f.existsAsFile())
                return;

            if (auto xml = juce::XmlDocument::parse (f))
                processor.loadStateXml (*xml);   // 走带守卫的入口，不要直接 replaceState

            notifyPresetName (f.getFileNameWithoutExtension(), true);
        });
}

void TOIRE2AWebEditor::handlePresetAction (const juce::Array<juce::var>& args)
{
    if (args.isEmpty())
        return;

    // payload 可能是 { action: "save" } 也可能直接是 "save"，两种都兼容
    juce::String action;
    if (args[0].isObject())
        action = args[0].getProperty ("action", juce::var()).toString();
    else
        action = args[0].toString();

    if (action == "save")
        savePresetAsDialog();
    else if (action == "load")
        loadPresetDialog();
    else if (action.startsWith ("load:"))
        loadUserPresetByName (action.substring (5));
}

void TOIRE2AWebEditor::loadUserPresetByName (const juce::String& name)
{
    if (name.isEmpty())
        return;

    // 只接受文件名主干，防目录穿越
    const auto safe = juce::File::createLegalFileName (name);
    const auto f = presetDirectory().getChildFile (safe + ".toire2a");
    if (! f.existsAsFile())
        return;

    if (auto xml = juce::XmlDocument::parse (f))
        processor.loadStateXml (*xml);

    notifyPresetName (name, true);
}

#endif // TOREI2A_HAS_WEBVIEW
