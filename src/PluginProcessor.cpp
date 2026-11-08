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
}

TOIRE2AAudioProcessor::~TOIRE2AAudioProcessor() = default;

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
        50.0f));

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

    // 表头 3 位开关（真机面板上的 GAIN REDUCTION / OUTPUT +10 / OUTPUT +4）
    //   0 = GR        指针显示增益衰减（压得越深越往左）
    //   1 = OUTPUT+10 0 VU ↔ −8 dBFS（即 +10 dBu 参考）
    //   2 = OUTPUT+4  0 VU ↔ −14 dBFS（即 +4 dBu 参考）
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

    return layout;
}

void TOIRE2AAudioProcessor::prepareToPlay (double sampleRate, int /*samplesPerBlock*/)
{
    compressor.prepare (sampleRate);
    if (peakReductionParam != nullptr)
        compressor.setPeakReduction (peakReductionParam->load() * 0.01f);
    if (makeupGainParam != nullptr)
        compressor.setMakeupGainDb (dialToGainDb (makeupGainParam->load()));
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

    // 旁路：直接跳过压缩器（buffer 是原地处理，不动就是直通）
    const bool bypassed = bypassParam != nullptr && bypassParam->load() > 0.5f;
    const float blockGr = bypassed ? 0.0f
                                   : compressor.process (chans, usedCh, numSamples);

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

    // ── 表头模式 ───────────────────────────────────────────
    //   0 = GR       ：指针 = GR（负值 → 越压越往左）
    //   1 = OUTPUT+10：0 VU ↔ −8 dBFS  → 值 = outDb + 8
    //   2 = OUTPUT+4 ：0 VU ↔ −14 dBFS → 值 = outDb + 14
    const int meterMode = meterModeParam != nullptr
                            ? (int) juce::jlimit (0.0f, 2.0f, meterModeParam->load())
                            : 0;
    float vuValueDb;
    if (meterMode == 0)
        vuValueDb = meterS2;
    else
        vuValueDb = meterPeakHold + ((meterMode == 1) ? 8.0f : 14.0f);

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

void TOIRE2AAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* createPluginFilter()
{
    return new TOIRE2AAudioProcessor();
}
