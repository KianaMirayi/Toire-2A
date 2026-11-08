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

    // ── 处理（原地）───────────────────────────────────────
    const int usedCh = juce::jmin (numCh, 2);
    float* chans[2]  = { nullptr, nullptr };
    for (int ch = 0; ch < usedCh; ++ch)
        chans[ch] = buffer.getWritePointer (ch);

    const float blockGr = compressor.process (chans, usedCh, numSamples);

    // ── DSP → 桥（原生 UI / WebView UI 共用）───────────────
    toreia2a::UiTelemetry t;
    t.gainReductionDb = blockGr;
    t.inputPeakDb     = toreia2a::UiBridge::linToDb (compressor.getInputPeak());
    t.outputPeakDb    = toreia2a::UiBridge::linToDb (compressor.getOutputPeak());
    t.vuDb            = t.gainReductionDb;
    t.frame           = ++frameCounter;
    uiBridge.publish (t);
}

juce::AudioProcessorEditor* TOIRE2AAudioProcessor::createEditor()
{
#if TOREI2A_HAS_WEBVIEW
    // 未来切换 WebView 前端时，把这里换成 Web 版 Editor 即可 —— DSP / 桥不动
    // return new TOIRE2AWebEditor (*this);
    return new TOIRE2AAudioProcessorEditor (*this);
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
