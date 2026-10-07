# The plug-in side of the FX sweep: loads a built PX3 Synth AU or VST3 through
# JUCE's hosting and runs the same sweep as `PX3Tests fxsweep`. A project of
# its own, not part of the main build - see README.md.
cmake_minimum_required(VERSION 3.22)
project(PX3FxSweepHost VERSION 0.1)

set(PX3_JUCE_DIR "" CACHE PATH "A JUCE checkout (e.g. <repo>/build/diag/_deps/juce-src)")
if (NOT PX3_JUCE_DIR)
    message(FATAL_ERROR "Pass -DPX3_JUCE_DIR=<path to JUCE>")
endif()
add_subdirectory(${PX3_JUCE_DIR} juce)

set(PX3_REPO ${CMAKE_CURRENT_LIST_DIR}/../..)
juce_add_console_app(PX3FxSweepHost PRODUCT_NAME "PX3FxSweepHost")
juce_generate_juce_header(PX3FxSweepHost)
target_sources(PX3FxSweepHost PRIVATE PluginHostSweep.cpp)
target_include_directories(PX3FxSweepHost PRIVATE ${PX3_REPO}/shared/DSP/Reverb ${CMAKE_CURRENT_LIST_DIR})
target_compile_definitions(PX3FxSweepHost PRIVATE JUCE_PLUGINHOST_AU=1 JUCE_PLUGINHOST_VST3=1 JUCE_USE_CURL=0
                           JUCE_WEB_BROWSER=0 JUCE_MODAL_LOOPS_PERMITTED=1)
target_link_libraries(PX3FxSweepHost PRIVATE juce::juce_audio_processors juce::juce_audio_utils
                      juce::juce_recommended_config_flags)
