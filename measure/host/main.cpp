// ReaMeasure —— 带实时日志区的 VST3 测量宿主（GUI）
//
// 打开即显示日志窗口，处理音频时实时滚动打印电平和增益。
//
// 用法：
//   ReaMeasure.exe --list-params  --plugin <vst3>
//   ReaMeasure.exe --sweep <i>    --plugin <vst3>
//   ReaMeasure.exe --plugin <vst3> --in <wav> --out <wav> [--param i=v ...] [--log file.txt]
//
// 注意：
//   * 插件创建 / 参数设置 / prepareToPlay 都在消息线程（UAD 插件否则静默失败）
//   * RMS 只看第 0 声道（输出可能是立体声但 ch1 为空）
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_extra/juce_gui_extra.h>

using namespace juce;

namespace
{

// ── 参数 ─────────────────────────────────────────────────
struct Options
{
    File plugin, inFile, outFile, logFile;
    std::vector<std::pair<int, float>> params;
    int  sweepIndex = -1;
    int  sweepSteps = 40;
    bool listParams = false;
    bool headless   = false;
};

Options parseArgs (const String& commandLine)
{
    Options o;
    auto args = StringArray::fromTokens (commandLine, true);
    args.removeEmptyStrings();

    for (int i = 0; i < args.size(); ++i)
    {
        const auto a = args[i];
        if (a == "--plugin" && i + 1 < args.size())            o.plugin   = File (args[++i].unquoted());
        else if (a == "--in"  && i + 1 < args.size())           o.inFile   = File (args[++i].unquoted());
        else if (a == "--out" && i + 1 < args.size())           o.outFile  = File (args[++i].unquoted());
        else if (a == "--log" && i + 1 < args.size())           o.logFile  = File (args[++i].unquoted());
        else if (a == "--list-params")                          o.listParams = true;
        else if (a == "--headless")                            o.headless   = true;
        else if (a == "--sweep" && i + 1 < args.size())         o.sweepIndex = args[++i].getIntValue();
        else if (a == "--sweep-steps" && i + 1 < args.size())   o.sweepSteps = jmax (2, args[++i].getIntValue());
        else if (a == "--param" && i + 1 < args.size())
        {
            const auto spec = args[++i];
            const int eq = spec.indexOfChar ('=');
            if (eq > 0) o.params.emplace_back (spec.substring (0, eq).getIntValue(),
                                               spec.substring (eq + 1).getFloatValue());
        }
    }
    return o;
}

std::unique_ptr<AudioPluginInstance> loadPlugin (const File& target, double sr, int blockSize, String& err)
{
    AudioPluginFormatManager fm;
    fm.addDefaultFormats();

    VST3PluginFormat* vst3 = nullptr;
    for (auto* f : fm.getFormats())
        if (auto* v = dynamic_cast<VST3PluginFormat*> (f)) { vst3 = v; break; }

    if (vst3 == nullptr) { err = "no VST3 format available"; return nullptr; }

    OwnedArray<PluginDescription> found;
    vst3->findAllTypesForFile (found, target.getFullPathName());
    if (found.isEmpty()) { err = "no VST3 plugin found at " + target.getFullPathName(); return nullptr; }

    String e;
    auto inst = fm.createPluginInstance (*found[0], sr, blockSize, e);
    if (inst == nullptr) err = e.isEmpty() ? "createPluginInstance failed" : e;
    return inst;
}

AudioBuffer<float> readWav (const File& f, double& sr)
{
    AudioFormatManager mgr;
    mgr.registerBasicFormats();
    std::unique_ptr<AudioFormatReader> r (mgr.createReaderFor (f));
    AudioBuffer<float> buf;
    if (r == nullptr) return buf;
    sr = r->sampleRate;
    buf.setSize ((int) r->numChannels, (int) r->lengthInSamples);
    r->read (&buf, 0, (int) r->lengthInSamples, 0, true, true);
    return buf;
}

bool writeWav (const File& f, const AudioBuffer<float>& buf, double sr)
{
    // 写 32-bit IEEE float WAV —— 插件的大 makeup 增益可能让输出超过 0 dBFS，
    // 用浮点存就不会削顶，也不需要事后等比缩放（缩放会污染 makeup 测量）。
    f.deleteFile();
    auto os = std::unique_ptr<FileOutputStream> (f.createOutputStream());
    if (os == nullptr) return false;

    const int    numCh   = buf.getNumChannels();
    const int64  numSmp  = buf.getNumSamples();
    const uint32 dataSz  = (uint32) (numSmp * numCh * 4);
    const uint32 riffSz  = 36 + dataSz;
    const uint32 byteRate = (uint32) (sr * numCh * 4);
    const uint16 align    = (uint16) (numCh * 4);

    char h[44];
    auto p32 = [] (char* p, uint32 v) { p[0]=(char)(v&0xff); p[1]=(char)((v>>8)&0xff); p[2]=(char)((v>>16)&0xff); p[3]=(char)((v>>24)&0xff); };
    auto p16 = [] (char* p, uint16 v) { p[0]=(char)(v&0xff); p[1]=(char)((v>>8)&0xff); };

    std::memcpy (h, "RIFF", 4);  p32 (h + 4, riffSz);
    std::memcpy (h + 8, "WAVEfmt ", 8);
    p32 (h + 16, 16);  p16 (h + 20, 3);            // format = 3 (IEEE float)
    p16 (h + 22, (uint16) numCh);  p32 (h + 24, (uint32) sr);
    p32 (h + 28, byteRate);  p16 (h + 32, align);  p16 (h + 34, 32);
    std::memcpy (h + 36, "data", 4);  p32 (h + 40, dataSz);

    if (! os->write (h, 44)) return false;

    // 交错写样本
    std::vector<float> inter ((size_t) (numSmp * numCh));
    for (int ch = 0; ch < numCh; ++ch)
    {
        const float* src = buf.getReadPointer (ch);
        for (int64 i = 0; i < numSmp; ++i)
            inter[(size_t) (i * numCh + ch)] = src[i];
    }
    const auto bytes = (int) (inter.size() * sizeof (float));
    if (! os->write (inter.data(), bytes)) return false;
    os->flush();
    return true;
}

double rmsDbCh0 (const AudioBuffer<float>& b, int start, int n)
{
    n = jmin (n, b.getNumSamples() - start);
    if (n <= 0 || b.getNumChannels() < 1) return -120.0;
    const float* p = b.getReadPointer (0, start);
    double sum = 0.0;
    for (int i = 0; i < n; ++i) sum += (double) p[i] * p[i];
    return 20.0 * std::log10 (jmax (std::sqrt (sum / n), 1e-12));
}

// ── 日志窗口 ─────────────────────────────────────────────
class LogComponent : public Component
{
public:
    LogComponent() : progress (progressValue)
    {
        addAndMakeVisible (header);
        header.setFont (Font (FontOptions (16.0f).withStyle ("Bold")));
        header.setColour (Label::textColourId, Colour (0xffe0b050));

        log.setMultiLine (true);
        log.setReadOnly (true);
        log.setScrollbarsShown (true);
        log.setFont (Font (FontOptions (Font::getDefaultMonospacedFontName(), 13.0f, Font::plain)));
        log.setColour (TextEditor::backgroundColourId, Colour (0xff141418));
        log.setColour (TextEditor::textColourId, Colour (0xffcfcfcf));
        log.setColour (TextEditor::outlineColourId, Colour (0xff33333a));
        addAndMakeVisible (log);

        addAndMakeVisible (progress);
        progress.setColour (ProgressBar::backgroundColourId, Colour (0xff232329));
        progress.setColour (ProgressBar::foregroundColourId, Colour (0xffe0b050));

        addAndMakeVisible (status);
        status.setText ("ready", dontSendNotification);
        status.setColour (Label::textColourId, Colour (0xff9a9aa4));
    }

    void setHeader (const String& t) { header.setText (t, dontSendNotification); }
    void setLogFile (const File& f)  { f.deleteFile(); logStream = f.createOutputStream(); }

    void resized() override
    {
        auto r = getLocalBounds().reduced (12);
        header.setBounds (r.removeFromTop (24));
        r.removeFromTop (6);
        status.setBounds (r.removeFromBottom (20));
        progress.setBounds (r.removeFromBottom (18));
        r.removeFromBottom (6);
        log.setBounds (r);
    }

    void line (const String& s, Colour c = Colour (0xffcfcfcf))
    {
        log.moveCaretToEnd();
        const auto old = log.findColour (TextEditor::textColourId);
        log.setColour (TextEditor::textColourId, c);
        log.insertTextAtCaret (s + "\n");
        log.setColour (TextEditor::textColourId, old);
        log.moveCaretToEnd();
        if (logStream != nullptr) { *logStream << s << "\n"; logStream->flush(); }
    }

    void setProgress (double p) { progressValue = jlimit (0.0, 1.0, p); progress.repaint(); }
    void setStatus (const String& s) { status.setText (s, dontSendNotification); }

private:
    double progressValue = 0.0;
    std::unique_ptr<FileOutputStream> logStream;

public:
    TextEditor  log;
    ProgressBar progress;
    Label       header, status;
};

// ── 音频处理线程 ─────────────────────────────────────────
class Worker : public Thread
{
public:
    Worker (LogComponent& c, AudioProcessor& p, AudioBuffer<float> in, double sr,
            File out, bool writeOutput, bool quitWhenDone)
        : Thread ("worker"), comp (c), plugin (p), input (std::move (in)),
          sampleRate (sr), outFile (out), shouldWrite (writeOutput),
          quitAfter (quitWhenDone) {}

    ~Worker() override { stopThread (5000); }

    void run() override
    {
        constexpr int blockSize = 512;
        const int total = input.getNumSamples();
        const int numCh = jmax (input.getNumChannels(), 2);

        AudioBuffer<float> out (numCh, total);
        out.clear();

        const int win = jmax (1, (int) (sampleRate * 0.25));
        int pos = 0;
        int nextReport = win;

        while (pos < total && ! threadShouldExit())
        {
            const int n = jmin (blockSize, total - pos);

            AudioBuffer<float> blk (numCh, n);
            blk.clear();
            for (int ch = 0; ch < input.getNumChannels(); ++ch)
                blk.copyFrom (ch, 0, input, ch, pos, n);

            MidiBuffer midi;
            plugin.processBlock (blk, midi);

            for (int ch = 0; ch < numCh; ++ch)
                out.copyFrom (ch, pos, blk, jmin (ch, blk.getNumChannels() - 1), 0, n);

            pos += n;

            while (pos >= nextReport && nextReport <= total)
            {
                const double t     = nextReport / sampleRate;
                const double inDb  = rmsDbCh0 (input, nextReport - win, win);
                const double outDb = rmsDbCh0 (out,   nextReport - win, win);
                const double gain  = outDb - inDb;
                emit (String::formatted ("%7.2fs   %9.2f   %9.2f   %+9.2f",
                                         t, inDb, outDb, gain));
                emitProgress (nextReport / (double) total);
                nextReport += win;
            }
        }

        if (shouldWrite)
        {
            // 只报告，不缩放 —— 32-bit float 能存超过 0 dBFS 的值
            float peak = 0.0f;
            for (int ch = 0; ch < out.getNumChannels(); ++ch)
                peak = jmax (peak, out.getMagnitude (ch, 0, out.getNumSamples()));
            if (peak > 0.99f)
                emit ("note: output peak " + String (20.0f * std::log10 (peak), 2)
                      + " dBFS (stored as float, no clipping)", Colour (0xffe0b050));

            const bool ok = writeWav (outFile, out, sampleRate);
            emit (ok ? ("done. wrote " + outFile.getFullPathName())
                     : ("ERROR writing " + outFile.getFullPathName()),
                  ok ? Colour (0xff7fc8ff) : Colour (0xffe05a5a));
        }
        else
        {
            emit ("done.", Colour (0xff7fc8ff));
        }
        emitProgress (1.0);
        emitStatus ("done");

        // headless 模式：跑完自动退出
        if (quitAfter)
            MessageManager::callAsync ([] {
                if (auto* app = JUCEApplication::getInstance())
                    app->systemRequestedQuit();
            });
    }

private:
    void emit (const String& s, Colour c = Colour (0xffcfcfcf))
    {
        MessageManager::callAsync ([safe = Component::SafePointer<LogComponent> (&comp), s, c]
        { if (safe != nullptr) safe->line (s, c); });
    }
    void emitProgress (double p)
    {
        MessageManager::callAsync ([safe = Component::SafePointer<LogComponent> (&comp), p]
        { if (safe != nullptr) safe->setProgress (p); });
    }
    void emitStatus (const String& s)
    {
        MessageManager::callAsync ([safe = Component::SafePointer<LogComponent> (&comp), s]
        { if (safe != nullptr) safe->setStatus (s); });
    }

    LogComponent& comp;
    AudioProcessor& plugin;
    AudioBuffer<float> input;
    double sampleRate;
    File outFile;
    bool shouldWrite;
    bool quitAfter = false;
};

// ── 应用 ─────────────────────────────────────────────────
class MeasureApp : public JUCEApplication
{
public:
    const String getApplicationName() override    { return "REA Measure"; }
    const String getApplicationVersion() override { return "0.2.0"; }
    bool moreThanOneInstanceAllowed() override    { return true; }

    void initialise (const String& commandLine) override
    {
        opts = parseArgs (commandLine);

        logComp = std::make_unique<LogComponent>();
        ui = logComp.get();
        if (! opts.headless)
            window = std::make_unique<Window> (*logComp, "REA Measure");

        if (opts.logFile != File()) ui->setLogFile (opts.logFile);
        ui->setHeader (opts.plugin == File() ? "(no plugin)" : opts.plugin.getFileName());

        if (! opts.plugin.exists())
        {
            ui->line ("ERROR: --plugin is required (or path does not exist)");
            ui->line ("usage:");
            ui->line ("  ReaMeasure.exe --list-params --plugin <vst3>");
            ui->line ("  ReaMeasure.exe --sweep 0 --plugin <vst3>");
            ui->line ("  ReaMeasure.exe --plugin <vst3> --in <wav> --out <wav> --param 0=0.5");
            ui->setStatus ("error");
            return;
        }

        // 默认采样率
        double sr = 48000.0;
        AudioBuffer<float> input;
        if (! opts.listParams && opts.sweepIndex < 0)
        {
            input = readWav (opts.inFile, sr);
            if (input.getNumSamples() == 0)
            {
                ui->line ("ERROR: cannot read input " + opts.inFile.getFullPathName());
                ui->setStatus ("error");
                return;
            }
        }

        String err;
        plugin = loadPlugin (opts.plugin, sr, 512, err);
        if (plugin == nullptr)
        {
            ui->line ("ERROR: plugin load failed: " + err, Colour (0xffe05a5a));
            ui->setStatus ("error");
            return;
        }

        ui->line ("plugin : " + plugin->getName(), Colour (0xff7fc8ff));
        ui->line ("params : " + String (plugin->getParameters().size()));
        ui->line ("latency: " + String (plugin->getLatencySamples()) + " samples");
        ui->line ("");

        auto& ps = plugin->getParameters();

        // ── 模式 1：列出参数 ──
        if (opts.listParams)
        {
            ui->line ("   idx  name                        steps      norm   text");
            ui->line ("----------------------------------------------------------------");
            for (int i = 0; i < ps.size(); ++i)
            {
                auto* p = ps[i];
                const String nm = p->getName (40);
                if (nm.startsWith ("MIDI CC")) continue;          // 过滤 JUCE 包装噪声

                // 注意：不要用 String::formatted 的 %s —— Windows 上会把 UTF-8 当成本地编码
                String ln;
                ln << "  " << String (i).paddedLeft (' ', 4)
                   << "  " << nm.paddedRight (' ', 26)
                   << "  " << String (p->getNumSteps()).paddedLeft (' ', 6)
                   << "  " << String (p->getValue(), 4).paddedLeft (' ', 6)
                   << "   " << p->getCurrentValueAsText();
                ui->line (ln);
            }
            ui->line ("");
            ui->line ("(MIDI CC parameters hidden)");
            ui->setStatus ("done");
            if (opts.headless) systemRequestedQuit();
            return;
        }

        // ── 模式 2：扫描参数 ──
        if (opts.sweepIndex >= 0)
        {
            if (opts.sweepIndex >= ps.size())
            {
                ui->line ("ERROR: sweep index out of range");
                ui->setStatus ("error");
                return;
            }
            auto* p = ps[opts.sweepIndex];
            ui->line ("sweep param[" + String (opts.sweepIndex) + "] '" + p->getName (40)
                      + "'  steps=" + String (p->getNumSteps()));
            ui->line ("     norm   readback   text");
            ui->line ("--------------------------------");
            for (int i = 0; i <= opts.sweepSteps; ++i)
            {
                const float v = (float) i / (float) opts.sweepSteps;
                p->setValueNotifyingHost (v);
                String ln;
                ln << "   " << String (v, 4).paddedLeft (' ', 6)
                   << "   " << String (p->getValue(), 4).paddedLeft (' ', 6)
                   << "   " << p->getCurrentValueAsText();
                ui->line (ln);
            }
            ui->setStatus ("done");
            if (opts.headless) systemRequestedQuit();
            return;
        }

        // ── 模式 3：处理音频 ──
        ui->line ("input  : " + String (input.getNumSamples() / sr, 2) + " s @ "
                  + String (sr, 0) + " Hz, " + String (input.getNumChannels()) + " ch");
        if (opts.outFile != File())
            ui->line ("output : " + opts.outFile.getFullPathName());

        for (auto& p : opts.params)
        {
            if (p.first < 0 || p.first >= ps.size()) continue;
            ps[p.first]->setValueNotifyingHost (jlimit (0.0f, 1.0f, p.second));
            String ln;
            ln << "param[" << String (p.first) << "] -> " << String (p.second, 3)
               << "  (" << ps[p.first]->getCurrentValueAsText() << ")";
            ui->line (ln);
        }

        plugin->prepareToPlay (sr, 512);

        ui->line ("");
        ui->line ("   time      in dBFS    out dBFS     gain dB");
        ui->line ("------------------------------------------------");

        worker = std::make_unique<Worker> (*logComp, *plugin, std::move (input), sr,
                                           opts.outFile, opts.outFile != File(), opts.headless);
        worker->startThread();
    }

    void shutdown() override
    {
        if (worker != nullptr) { worker->signalThreadShouldExit(); worker->stopThread (5000); worker.reset(); }
        if (plugin != nullptr) { plugin->releaseResources(); plugin.reset(); }
        window.reset();
    }

    void systemRequestedQuit() override { quit(); }

private:
    struct Window : DocumentWindow
    {
        Window (LogComponent& c, const String& title)
            : DocumentWindow (title, Colour (0xff141418), DocumentWindow::allButtons), content (c)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (&content, false);
            setResizable (true, true);
            setSize (1000, 660);
            centreWithSize (getWidth(), getHeight());
            setVisible (true);
            toFront (true);
        }
        void closeButtonPressed() override { JUCEApplication::getInstance()->systemRequestedQuit(); }
        LogComponent& content;
    };

    std::unique_ptr<LogComponent> logComp;
    std::unique_ptr<Window> window;
    std::unique_ptr<AudioPluginInstance> plugin;
    std::unique_ptr<Worker> worker;
    LogComponent* ui = nullptr;
    Options opts;
};

} // namespace

START_JUCE_APPLICATION (MeasureApp)
