/*
  ─────────────────────────────────────────────────────────────────────────────
   PluginEditorFactory.cpp —— 只负责把"编辑器"接进来。

   为什么要单独一个编译单元：
   createEditor() 是唯一需要同时看见 PluginProcessor 和 PluginEditor 的地方。
   放在 PluginProcessor.cpp 里会让 Processor 的编译单元吃进整套 UI
   （两套编辑器 + LookAndFeel + TOREI2A_HAS_WEBVIEW 分支），
   于是改一行 UI 就要重编 Processor，两种前端配置也得整份重编。

   把这里单独拆出来之后：
     PluginProcessor.cpp  →  只依赖 DSP / Bridge / Presets
     本文件                →  依赖 UI，但内容只有几行
  ─────────────────────────────────────────────────────────────────────────────
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

juce::AudioProcessorEditor* TOIRE2AAudioProcessor::createEditor()
{
#if TOREI2A_HAS_WEBVIEW
    // WebView2 前端（CMake: -DTOIRE2A_WEBVIEW2=ON）
    return new TOIRE2AWebEditor (*this);
#else
    return new TOIRE2AAudioProcessorEditor (*this);
#endif
}
