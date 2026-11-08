cmake_minimum_required(VERSION 3.22)
project(ReaMeasure VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JUCE 在 C++Hell/JUCE（本文件在 TOiRE-2A/measure/host/）
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../../JUCE ${CMAKE_BINARY_DIR}/juce)

# ─────────────────────────────────────────────────────────────
# ReaMeasure —— 带实时日志区的测量宿主（GUI）
#   list-params / sweep / 处理音频，全部在同一个窗口里滚动显示
# ─────────────────────────────────────────────────────────────
juce_add_gui_app(ReaMeasure PRODUCT_NAME "rea-measure")

target_sources(ReaMeasure PRIVATE main.cpp)

target_compile_definitions(ReaMeasure PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_PLUGINHOST_VST3=1
    JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_APPLICATION_NAME_STRING="REA Measure"
    JUCE_APPLICATION_VERSION_STRING="0.2.0")

target_link_libraries(ReaMeasure
    PRIVATE
        juce::juce_audio_processors
        juce::juce_audio_utils
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags)

if(MSVC)
    target_compile_options(ReaMeasure PRIVATE /utf-8)
endif()

# ─────────────────────────────────────────────────────────────
# ReaViewer —— 显示插件的编辑器界面
# ─────────────────────────────────────────────────────────────
juce_add_gui_app(ReaViewer PRODUCT_NAME "rea-viewer")

target_sources(ReaViewer PRIVATE viewer.cpp)

target_compile_definitions(ReaViewer PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_PLUGINHOST_VST3=1
    JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_APPLICATION_NAME_STRING="REA Viewer"
    JUCE_APPLICATION_VERSION_STRING="0.1.0")

target_link_libraries(ReaViewer
    PRIVATE
        juce::juce_audio_processors
        juce::juce_audio_utils
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags)

if(MSVC)
    target_compile_options(ReaViewer PRIVATE /utf-8)
endif()
