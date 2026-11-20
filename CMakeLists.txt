cmake_minimum_required(VERSION 3.22)
project(TOIRE2A VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ─────────────────────────────────────────────────────────────
# 前端切换开关
#   OFF（默认）：原生 JUCE UI（CTRL-2A 当前阶段）
#   ON         ：WebView2 + Web UI（后期切换）
# 切换只需改这个选项，DSP / Bridge 层完全不动。
# ─────────────────────────────────────────────────────────────
option(TOIRE2A_WEBVIEW2 "Build with WebView2 backend for the future web UI" OFF)

add_subdirectory(../JUCE ${CMAKE_BINARY_DIR}/juce)

juce_add_plugin(TOIRE2A
    COMPANY_NAME "TOREI"
    PLUGIN_MANUFACTURER_CODE Tore
    PLUGIN_CODE T2A1
    FORMATS VST3 Standalone
    PRODUCT_NAME "TOIRE-2A"
    VERSION 0.1.0
    COPY_PLUGIN_AFTER_BUILD TRUE
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE)

# ⚠️ JUCE 8: 必须在 target_sources 之前生成 JuceHeader.h
juce_generate_juce_header(TOIRE2A)

target_sources(TOIRE2A PRIVATE
    src/PluginProcessor.cpp
    src/PluginEditor.cpp
    src/PluginEditorFactory.cpp
    src/DSP/LA2ACompressor.cpp)

target_include_directories(TOIRE2A PRIVATE src)

target_compile_definitions(TOIRE2A PUBLIC
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

# ⚠️ MSVC 读 UTF-8 无 BOM 源码会按 GBK 解，中文注释里的 0x5C 会吞掉下一行
if(MSVC)
    target_compile_options(TOIRE2A PRIVATE /utf-8)
endif()

target_link_libraries(TOIRE2A
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

# ─────────────────────────────────────────────────────────────
# WebView2 前端
#   ON ：WebView2 + WebUI/（网页前端）
#   OFF：原生 JUCE UI
# 切换只改这一个选项，DSP / Bridge 层完全不动。
# ─────────────────────────────────────────────────────────────
if(TOIRE2A_WEBVIEW2)
    # NuGet 包里的 WebView2 SDK（build 是符号链接 -> 1.0.1901.177/build）
    set(WEBVIEW2_SDK "C:/Users/Admin/.nuget/packages/Microsoft.Web.WebView2")
    target_include_directories(TOIRE2A PRIVATE "${WEBVIEW2_SDK}/build/native/include")

    target_compile_definitions(TOIRE2A PUBLIC
        JUCE_WEB_BROWSER=1
        JUCE_USE_WIN_WEBVIEW2=1
        TOREI2A_HAS_WEBVIEW=1)

    target_link_libraries(TOIRE2A PRIVATE juce::juce_gui_extra)

    # ── 前端资源 + WebView2Loader.dll 一起摆到产物旁边 ──────────
    #   Standalone/TOIRE-2A.exe        ← 同目录 webui/
    #   VST3/TOIRE-2A.vst3/Contents/   ← Contents/webui/
    set(WEBUI_SRC   "${CMAKE_CURRENT_SOURCE_DIR}/WebUI")
    set(WEBVIEW2_DLL "${WEBVIEW2_SDK}/build/native/x64/WebView2Loader.dll")

    add_custom_command(TARGET TOIRE2A POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${WEBUI_SRC}" "$<TARGET_FILE_DIR:TOIRE2A>/Standalone/webui"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${WEBUI_SRC}" "$<TARGET_FILE_DIR:TOIRE2A>/VST3/TOIRE-2A.vst3/Contents/webui"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${WEBVIEW2_DLL}" "$<TARGET_FILE_DIR:TOIRE2A>/Standalone/WebView2Loader.dll"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${WEBVIEW2_DLL}" "$<TARGET_FILE_DIR:TOIRE2A>/VST3/TOIRE-2A.vst3/Contents/x86_64-win/WebView2Loader.dll"
        COMMENT "TOIRE-2A: deploying webui/ + WebView2Loader.dll"
        VERBATIM)

    message(STATUS "TOIRE-2A: WebView2 frontend ENABLED  (webui -> Standalone/ + VST3/Contents/)")
else()
    target_compile_definitions(TOIRE2A PUBLIC
        JUCE_WEB_BROWSER=0
        TOREI2A_HAS_WEBVIEW=0)
    message(STATUS "TOIRE-2A: native UI (WebView2 disabled)")
endif()
