#include "PluginProcessor.h"

#include "PluginEditor.h"

TOIRE2AAudioProcessor::TOIRE2AAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createParameterLayout())
{
    peakReductionParam = apvts.getRawParameterValue ("peakReduction");
    makeupGainParam    = apvts.getRawParameterValue ("makeupGain");
    meterAttackParam   = apvts.getRawParameterValue ("meterAttack");
    meterReleaseParam  = apvts.getRawParameterValue ("meterRelease");
    meterModeParam     = apvts.getRawParameterValue ("meterMode");
    bypassParam        = apvts.getRawParameterValue ("bypass");

    // 预设监听放在最后：确保所有 raw 指针都已就绪
    apvts.addParameterListener ("preset", this);
}

TOIRE2AAudioProcessor::~TOIRE2AAudioProcessor()
{
    apvts.removeParameterListener ("preset", this);
}

//==============================================================================
//  预设
//==============================================================================
namespace
{
    // 写一个参数但不从这里触发监听回调（applyingPreset 已经挡住了）
    void setParamSilently (juce::AudioProcessorValueTreeState& apvts,
                           const juce::String& id, float normalised)
    {
        if (auto* p = apvts.getParameter (id))
            p->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, normalised));
    }
}

void TOIRE2AAudioProcessor::applyPreset (int index)
{
    if (index < 0 || index >= toreia2a::kNumFactoryPresets)
        return;

    const auto& pr = toreia2a::kFactoryPresets[index];
    currentPreset.store (index, std::memory_order_release);

    applyingPreset.store (true, std::memory_order_release);
    struct ApplyGuard { std::atomic<bool>& f; ~ApplyGuard() { f.store (false, std::memory_order_release); } } guard { applyingPreset };

    setParamSilently (apvts, "peakReduction", pr.peakReduction * 0.01f);
    setParamSilently (apvts, "makeupGain",    pr.makeupGain    * 0.01f);
    setParamSilently (apvts, "meterMode",     (float) pr.meterMode / 2.0f);
}

void TOIRE2AAudioProcessor::parameterChanged (const juce::String& parameterID, float newValue)
{
    if (applyingPreset.load (std::memory_order_acquire)
        || loadingState.load (std::memory_order_acquire))
        return;

    if (parameterID == "preset")
    {
        // APVTS 的监听回调给的是【非归一化】值，直接用
        const int idx = juce::jlimit (0, toreia2a::kNumFactoryPresets - 1,
                                      (int) std::lround (newValue));
        if (idx == currentPreset.load (std::memory_order_acquire))
            return;

        // 消息线程（网页下拉）→ 立即应用；宿主/音频线程 → 延后，避免在音频线程改 APVTS
        if (juce::MessageManager::existsAndIsCurrentThread())
        {
            applyPreset (idx);
        }
        else
        {
            pendingPreset.store (idx, std::memory_order_release);
            triggerAsyncUpdate();
        }
    }
}

void TOIRE2AAudioProcessor::handleAsyncUpdate()
{
    const int idx = pendingPreset.exchange (-1, std::memory_order_acq_rel);
    if (idx >= 0 && idx != currentPreset.load (std::memory_order_acquire))
        applyPreset (idx);
}

void TOIRE2AAudioProcessor::setCurrentProgram (int index)
{
    applyPreset (juce::jlimit (0, toreia2a::kNumFactoryPresets - 1, index));

    // 同步给网页下拉（如果编辑器开着）
    if (auto* p = apvts.getParameter ("preset"))
        p->setValueNotifyingHost (p->convertTo0to1 ((float) currentPreset.load (std::memory_order_acquire)));
}

const juce::String TOIRE2AAudioProcessor::getProgramName (int index)
{
    if (index >= 0 && index < toreia2a::kNumFactoryPresets)
        return juce::String (toreia2a::kFactoryPresets[index].name);
    return {};
}

namespace
{
/**
 * Gain 拨盘 0..100 → makeup dB，由 UAD LA-2A 逐点实测（14 个点）。
 *
 * 这条曲线高度非线性：
 *   0–10  是"关闭区"，拨盘 10 时已经只有 −10.55 dB
 *   10–50 约 0.63 dB/格
 *   90–100 接近饱和（只涨 1.4 dB）
 * 之前用 4 点表插值，中段差 1–2 dB、低端差 11 dB，是听感不对的主因。
 */
constexpr int kGainPts = 14;
const float kGainDial[kGainPts] = { 0,   5,     10,     15,    20,    25,   30,    40,
                                    50,  60,    70,     80,    90,    100 };
const float kGainDb[kGainPts]   = { -100.0f, -59.59f, -10.55f, -4.46f, -0.32f, 2.57f, 5.49f, 11.06f,
                                    14.50f, 18.43f,  24.95f,  31.46f, 36.29f, 37.70f };

float dialToGainDb (float dial)
{
    dial = juce::jlimit (0.0f, 100.0f, dial);
    for (int i = 1; i < kGainPts; ++i)
    {
        if (dial <= kGainDial[i])
        {
            const float t = (dial - kGainDial[i - 1]) / (kGainDial[i] - kGainDial[i - 1]);
            return kGainDb[i - 1] + t * (kGainDb[i] - kGainDb[i - 1]);
        }
    }
    return kGainDb[kGainPts - 1];
}
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout
TOIRE2AAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // Peak Reduction：0..100 拨盘（对应真机刻度），内部映射到 0..1 的侧链驱动量
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "peakReduction", 1 },
        "Peak Reduction",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f),
        30.0f));

    // Gain：0..100 拨盘，内部按实测表映射成 dB
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "makeupGain", 1 },
        "Gain",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f),
        30.0f));

    // ── 表头弹道（VU 积分器）─────────────────────────────
    // 暴露出来方便现场调：起针越慢，两个快速底鼓越不会"跳两下"。
    //   真 VU 规范：满幅上升 ≈300 ms（二阶 τ≈60 ms）
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "meterAttack", 1 },
        "Meter Attack",
        juce::NormalisableRange<float> (5.0f, 300.0f, 1.0f),
        62.0f));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "meterRelease", 1 },
        "Meter Release",
        juce::NormalisableRange<float> (30.0f, 2000.0f, 1.0f),
        110.0f));

    // 表头 3 位开关：0=GR  1=OUTPUT+10（0VU↔-12dBFS）  2=OUTPUT+4（0VU↔-18dBFS）
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "meterMode", 1 },
        "Meter Mode",
        juce::StringArray { "Gain Reduction", "Output +10", "Output +4" },
        0));

    // 旁路开关（面板上的 POWER / ON）
    //   true  = 旁路，直通不压缩
    //   false = 正常处理
    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "bypass", 1 },
        "Bypass",
        false));

    // 预设号（0..kNumFactoryPresets-1）。不参与自动化，只作为「网页下拉 → C++」的通道。
    // 用非自动化的 AudioParameterInt 而不是自定义事件，是为了复用现成的 slider relay。
    // JUCE 8 没有公开的 setAutomatable()，所以用子类覆盖 isAutomatable()。
    struct PresetParameter : juce::AudioParameterInt
    {
        using juce::AudioParameterInt::AudioParameterInt;
        bool isAutomatable() const override { return false; }
    };

    layout.add (std::make_unique<PresetParameter> (
        juce::ParameterID { "preset", 1 },
        "Preset",
        0, toreia2a::kNumFactoryPresets - 1,
        0));

    return layout;
}

void TOIRE2AAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    // 先设参数，再 prepare —— prepare 会把平滑器吸附到目标值，避免启动渐入
    if (peakReductionParam != nullptr)
        compressor.setPeakReduction (peakReductionParam->load() * 0.01f);
    if (makeupGainParam != nullptr)
        compressor.setMakeupGainDb (dialToGainDb (makeupGainParam->load()));

    compressor.prepare (sampleRate);

    // 旁路交叉淡化用的干声暂存（只在这里分配，音频线程零分配）
    dryScratch.setSize (2, juce::jmax (1, samplesPerBlock), false, false, true);

    // 起始就对齐当前旁路状态：否则"全程旁路"的渲染会在前 20 ms 淡变，
    // 破坏"旁路 = 纯直通（逐采样等于干声）"这条性质。
    bypassAmount = (bypassParam != nullptr && bypassParam->load() > 0.5f) ? 1.0f : 0.0f;

    // 表头状态归零，避免换采样率 / 换缓冲后带着旧值回来
    meterS1 = 0.0f;
    meterS2 = 0.0f;
    meterPeakHold = -96.0f;
}

void TOIRE2AAudioProcessor::releaseResources() {}

bool TOIRE2AAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& mainIn  = layouts.getMainInputChannelSet();
    const auto& mainOut = layouts.getMainOutputChannelSet();

    if (mainIn != mainOut)
        return false;

    return mainOut == juce::AudioChannelSet::mono()
        || mainOut == juce::AudioChannelSet::stereo();
}

void TOIRE2AAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                          juce::MidiBuffer& /*midi*/)
{
    juce::ScopedNoDenormals noDenormals;

    const int numCh      = buffer.getNumChannels();
    const int numSamples = buffer.getNumSamples();

    if (numCh == 0 || numSamples == 0)
        return;

    // ── 参数 → DSP（拨盘 0..100 转成内部量）─────────────
    compressor.setPeakReduction (peakReductionParam->load() * 0.01f);
    compressor.setMakeupGainDb  (dialToGainDb (makeupGainParam->load()));

    // 表头弹道（可在 UI 里实时调）
    const float meterAtkMs = meterAttackParam  != nullptr ? meterAttackParam->load()  : 62.0f;
    const float meterRelMs = meterReleaseParam != nullptr ? meterReleaseParam->load() : 110.0f;

    // ── 处理（原地）───────────────────────────────────────
    const int usedCh = juce::jmin (numCh, 2);
    float* chans[2]  = { nullptr, nullptr };
    for (int ch = 0; ch < usedCh; ++ch)
        chans[ch] = buffer.getWritePointer (ch);

    // ── 旁路交叉淡化 ────────────────────────────────────────
    // 硬切会让输出一步跨越整机净增益（高 Gain 时十几 dB）→ pop。
    // 这里改成 20 ms 淡变；而且旁路期间仍然跑 compressor，
    // 让内部状态（dcState / bright / currentGain）保持新鲜，解除时不会再补一次咔哒。
    // ⚠️ 常态（未旁路且已收敛）走的是零开销分支：不拷干声、不混合，
    //    buffer 完全由 compressor 原地处理 → 输出与改动前逐采样相同。
    const bool bypassed = bypassParam != nullptr && bypassParam->load() > 0.5f;
    const float targetBypass = bypassed ? 1.0f : 0.0f;
    const bool crossfading = (bypassAmount > 0.0f) || (targetBypass > 0.0f);

    const bool canCrossfade = crossfading
                              && dryScratch.getNumSamples() >= numSamples
                              && dryScratch.getNumChannels() >= usedCh;

    if (canCrossfade)
        for (int ch = 0; ch < usedCh; ++ch)
            dryScratch.copyFrom (ch, 0, buffer, ch, 0, numSamples);

    // 始终处理（旁路期间也跑，保持状态新鲜）
    float blockGr = compressor.process (chans, usedCh, numSamples);

    if (canCrossfade)
    {
        const float step = (targetBypass - bypassAmount) / (float) numSamples;
        for (int n = 0; n < numSamples; ++n)
        {
            bypassAmount += step;
            const float w = juce::jlimit (0.0f, 1.0f, bypassAmount);
            for (int ch = 0; ch < usedCh; ++ch)
                chans[ch][n] = chans[ch][n] * (1.0f - w) + dryScratch.getReadPointer (ch)[n] * w;
        }
        bypassAmount = targetBypass;
        blockGr *= (1.0f - bypassAmount);   // 表头跟着淡出
    }
    else if (bypassed)
    {
        blockGr = 0.0f;                     // 纯旁路（无缓冲可混合时的兜底）
    }

    // 表头弹道（二阶 VU 积分器，不是取峰值）──────────────
    // 直接送"块内最深 GR"会让指针跟着每个尖峰乱跳（两个快速底鼓会看到跳两下）。
    // 真表头是二阶低通：满幅上升 ~300 ms、无过冲，第二个底鼓的尖峰会被抹平。
    {
        const float blockSec = (float) numSamples / (float) juce::jmax (1.0, getSampleRate());
        const float tau = ((blockGr < meterS2) ? meterAtkMs : meterRelMs) * 0.001f;
        const float a = 1.0f - std::exp (-blockSec / tau);
        meterS1 += (blockGr - meterS1) * a;
        meterS2 += (meterS1 - meterS2) * a;
    }

    // 输出峰值（表头 OUTPUT +10 / +4 模式用），慢释放
    {
        const float inPk  = compressor.getInputPeak();
        const float outPk = compressor.getOutputPeak();
        const float pkDb  = (outPk > 1.58e-5f) ? 20.0f * std::log10 (outPk) : -96.0f;
        const float sec   = (float) numSamples / (float) juce::jmax (1.0, getSampleRate());
        const float aPk   = 1.0f - std::exp (-sec / 0.4f);
        meterPeakHold += (pkDb - meterPeakHold) * aPk;
        (void) inPk;
    }

    // 表头模式：GR 用二阶积分后的 meterS2；输出档用峰值 + 参考电平偏移
    // 两档相差 6 dB 是真机的表头灵敏度差，参考点按 -18 dBFS = +4 dBu
    const int meterMode = meterModeParam != nullptr
                            ? (int) juce::jlimit (0.0f, 2.0f, meterModeParam->load())
                            : 0;
    float vuValueDb;
    if (meterMode == 0)
        vuValueDb = meterS2;
    else
        vuValueDb = meterPeakHold + ((meterMode == 1) ? 12.0f : 18.0f);

    // ── DSP → 桥（原生 UI / WebView UI 共用）───────────────
    toreia2a::UiTelemetry t;
    t.gainReductionDb = vuValueDb;
    t.inputPeakDb     = toreia2a::UiBridge::linToDb (compressor.getInputPeak());
    t.outputPeakDb    = toreia2a::UiBridge::linToDb (compressor.getOutputPeak());
    t.vuDb            = t.gainReductionDb;
    t.frame           = ++frameCounter;
    uiBridge.publish (t);
}

juce::AudioProcessorEditor* TOIRE2AAudioProcessor::createEditor()
{
#if TOREI2A_HAS_WEBVIEW
    // WebView2 前端（CMake: -DTOIRE2A_WEBVIEW2=ON）
    return new TOIRE2AWebEditor (*this);
#else
    return new TOIRE2AAudioProcessorEditor (*this);
#endif
}

void TOIRE2AAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto state = apvts.copyState(); state.isValid())
    {
        std::unique_ptr<juce::XmlElement> xml (state.createXml());
        copyXmlToBinary (*xml, destData);
    }
}

namespace
{
    /** RAII：装载期间把 loadingState 置 true，析构时恢复原值（异常也安全）*/
    struct AtomicFlagScope
    {
        std::atomic<bool>& ref;
        bool prev;
        explicit AtomicFlagScope (std::atomic<bool>& r)
            : ref (r), prev (r.exchange (true, std::memory_order_acq_rel)) {}
        ~AtomicFlagScope() { ref.store (prev, std::memory_order_release); }
        JUCE_DECLARE_NON_COPYABLE (AtomicFlagScope)
    };
}

void TOIRE2AAudioProcessor::loadStateXml (const juce::XmlElement& xml)
{
    if (! xml.hasTagName (apvts.state.getType()))
        return;

    // 装载期间屏蔽 "preset" 监听器：replaceState 会逐个参数触发
    // setValueNotifyingHost，否则 applyPreset 会把刚恢复的值覆写出厂预设。
    // 同时把 currentPreset 与文件里存的 preset 号对齐，避免之后误触发。
    {
        const AtomicFlagScope guard (loadingState);
        apvts.replaceState (juce::ValueTree::fromXml (xml));
    }

    if (auto* p = apvts.getParameter ("preset"))
        currentPreset.store (juce::jlimit (0, toreia2a::kNumFactoryPresets - 1,
                                            (int) std::lround (p->convertFrom0to1 (p->getValue()))),
                             std::memory_order_release);
}

void TOIRE2AAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        loadStateXml (*xml);
}

juce::AudioProcessor* createPluginFilter()
{
    return new TOIRE2AAudioProcessor();
}
