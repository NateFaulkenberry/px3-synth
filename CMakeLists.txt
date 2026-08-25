cmake_minimum_required(VERSION 3.22)

# Single source of truth for plugin/project SemVer.
set(PX3_VERSION "0.1.0")

if (NOT PX3_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR "PX3_VERSION must use SemVer MAJOR.MINOR.PATCH (example: 0.1.0).")
endif()

project(PX3Synth VERSION ${PX3_VERSION} LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(PX3_COPY_PLUGIN_AFTER_BUILD "Copy plugin formats into local plugin folders after building" ON)
option(PX3_DEBUG_PANEL "Enable in-plugin debug panel UI" OFF)

include(FetchContent)

# Reproducible JUCE dependency via FetchContent.
FetchContent_Declare(
    juce
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.4
)

set(FETCHCONTENT_UPDATES_DISCONNECTED ON)
FetchContent_MakeAvailable(juce)

juce_add_plugin(PX3Synth
    COMPANY_NAME "PX3"
    BUNDLE_ID "com.px3.px3synth"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD ${PX3_COPY_PLUGIN_AFTER_BUILD}
    PLUGIN_MANUFACTURER_CODE SyPr
    PLUGIN_CODE SyP1
    FORMATS AU VST3 Standalone
    PRODUCT_NAME "PX3 Synth"
)

juce_generate_juce_header(PX3Synth)

juce_add_binary_data(PX3Assets
    SOURCES
        Source/pp.png
        Source/px3.gif
)

target_sources(PX3Synth
    PRIVATE
        Source/UI/PerformanceControls.cpp
        Source/UI/PianoKeyboard.cpp
        Source/UI/OscPanel.cpp
        Source/UI/EnvPanel.cpp
        Source/UI/FltPanel.cpp
        Source/UI/FxPanel.cpp
        Source/UI/MixPanel.cpp
        Source/UI/TopMenuBar.cpp
        Source/UI/DelayComponent.cpp
        Source/UI/EnvelopeComponent.cpp
        Source/UI/FilterComponent.cpp
        Source/UI/LfoComponent.cpp
        Source/UI/ReverbUiComponent.cpp
        Source/UI/SubOscComponent.cpp
        Source/UI/VibeComponent.cpp
        Source/UI/OscillatorComponent.cpp
        Source/UI/PluginEditor.cpp
        Source/UI/PluginEditorDebug.cpp
        Source/DSP/EnvelopeGenerator.cpp
        Source/DSP/Delay.cpp
        Source/DSP/LfoGenerator.cpp
        Source/DSP/ReverbComponent.cpp
        Source/DSP/SubOscillator.cpp
        Source/DSP/Vibe.cpp
        Source/DSP/PluginProcessor.cpp
        Source/DSP/PluginProcessorParameters.cpp
        Source/DSP/PluginProcessorMidi.cpp
        Source/DSP/PluginProcessorDebug.cpp
        Source/DSP/PluginProcessorSource.cpp
        Source/DSP/PluginProcessorEffects.cpp
        Source/DSP/PluginProcessorState.cpp
        Source/DSP/SynthSound.cpp
        Source/DSP/VibeEngine.cpp
        Source/DSP/OscillatorUnit.cpp
        Source/DSP/VoiceFilter.cpp
        Source/DSP/SynthVoice.cpp
        Source/Preset/PresetManager.cpp
)

target_include_directories(PX3Synth
    PRIVATE
        Source
        Source/UI
        Source/DSP
        Source/Preset
        Source/Core
)

target_compile_definitions(PX3Synth
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
    PX3_DEBUG_PANEL=$<IF:$<BOOL:${PX3_DEBUG_PANEL}>,1,0>
)

target_link_libraries(PX3Synth
    PRIVATE
        PX3Assets
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
)

if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    set(PX3_WARNING_OPTIONS
        -Wall
        -Wextra
        -Wpedantic
        -Wshadow
        -Wconversion
        -Wsign-conversion
        -Wno-nontrivial-memcall
        -Wno-switch-enum
        -Wno-unused-but-set-variable
        -Wno-unnecessary-virtual-specifier
    )

    # Apply warning policy to the shared target and JUCE-generated format targets.
    foreach(px3Target IN ITEMS
            PX3Synth
            PX3Synth_Standalone
            PX3Synth_AU
            PX3Synth_VST3)
        if (TARGET ${px3Target})
            target_compile_options(${px3Target} PRIVATE ${PX3_WARNING_OPTIONS})
        endif()
    endforeach()
endif()
