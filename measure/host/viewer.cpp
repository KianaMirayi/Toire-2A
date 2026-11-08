// rea-viewer —— 用 GUI 子系统加载 VST3 并显示它的编辑器
// （WebView2 类的插件编辑器在控制台程序里会崩，必须用 GUI app）
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_extra/juce_gui_extra.h>

using namespace juce;

namespace
{

std::unique_ptr<AudioPluginInstance> loadPlugin (const File& target, double sr, int blockSize, String& err)
{
    AudioPluginFormatManager fm;
    fm.addDefaultFormats();

    VST3PluginFormat* vst3 = nullptr;
    for (auto* f : fm.getFormats())
        if (auto* v = dynamic_cast<VST3PluginFormat*> (f)) { vst3 = v; break; }

    if (vst3 == nullptr) { err = "no VST3 format"; return nullptr; }

    OwnedArray<PluginDescription> found;
    vst3->findAllTypesForFile (found, target.getFullPathName());
    if (found.isEmpty()) { err = "no plugin at " + target.getFullPathName(); return nullptr; }

    String e;
    auto inst = fm.createPluginInstance (*found[0], sr, blockSize, e);
    if (inst == nullptr) err = e.isEmpty() ? "createPluginInstance failed" : e;
    return inst;
}

class PluginWindow : public DocumentWindow
{
public:
    PluginWindow (AudioProcessor& p, const String& title)
        : DocumentWindow (title, Colours::black, DocumentWindow::allButtons), proc (p)
    {
        setUsingNativeTitleBar (true);
        setResizable (true, false);

        if (auto* ed = proc.createEditorIfNeeded())
        {
            setContentOwned (ed, true);
            setSize (jmax (400, ed->getWidth() + 12), jmax (200, ed->getHeight() + 34));
        }
        else
        {
            setSize (900, 300);
        }

        centreWithSize (getWidth(), getHeight());
        setVisible (true);
        toFront (true);
    }

    ~PluginWindow() override
    {
        if (auto* ed = dynamic_cast<AudioProcessorEditor*> (getContentComponent()))
            proc.editorBeingDeleted (ed);
        clearContentComponent();
    }

    void closeButtonPressed() override { JUCEApplication::getInstance()->systemRequestedQuit(); }

private:
    AudioProcessor& proc;
};

class ViewerApp : public JUCEApplication
{
public:
    const String getApplicationName() override    { return "REA Viewer"; }
    const String getApplicationVersion() override { return "0.1.0"; }
    bool moreThanOneInstanceAllowed() override    { return true; }

    void initialise (const String& commandLine) override
    {
        logFile = File::getSpecialLocation (File::tempDirectory).getChildFile ("rea-viewer.log");
        logFile.deleteFile();
        logger = std::make_unique<FileLogger> (logFile, "rea-viewer", 0);
        Logger::setCurrentLogger (logger.get());

        Logger::writeToLog ("init, commandLine = [" + commandLine + "]");

        StringArray args = StringArray::fromTokens (commandLine, true);
        args.removeEmptyStrings();
        Logger::writeToLog ("argc=" + String (args.size()) + "  args=" + args.joinIntoString (" | "));

        String pluginPath;
        double sr = 48000.0;
        for (int i = 0; i < args.size(); ++i)
        {
            if (args[i] == "--plugin" && i + 1 < args.size())      pluginPath = args[++i].unquoted();
            else if (args[i] == "--sr" && i + 1 < args.size())     sr = args[++i].getDoubleValue();
            else if (! args[i].startsWith ("--") && pluginPath.isEmpty()) pluginPath = args[i].unquoted();
        }

        if (pluginPath.isEmpty())
        {
            AlertWindow::showMessageBoxAsync (MessageBoxIconType::WarningIcon,
                                              "REA Viewer", "usage: rea-viewer --plugin <vst3 path>");
            quit();
            return;
        }

        String err;
        plugin = loadPlugin (File (pluginPath), sr, 512, err);
        if (plugin == nullptr)
        {
            AlertWindow::showMessageBoxAsync (MessageBoxIconType::WarningIcon, "REA Viewer",
                                              "load failed: " + err);
            quit();
            return;
        }

        plugin->prepareToPlay (sr, 512);
        Logger::writeToLog ("loaded: " + plugin->getName() + "  latency=" + String (plugin->getLatencySamples()));

        window = std::make_unique<PluginWindow> (*plugin, plugin->getName());
    }

    void shutdown() override
    {
        window.reset();
        if (plugin != nullptr) { plugin->releaseResources(); plugin.reset(); }
    }

    void systemRequestedQuit() override { quit(); }

private:
    std::unique_ptr<AudioPluginInstance> plugin;
    std::unique_ptr<PluginWindow> window;
    std::unique_ptr<FileLogger> logger;
    File logFile;
};

} // namespace

START_JUCE_APPLICATION (ViewerApp)
