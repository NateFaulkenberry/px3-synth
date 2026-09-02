cmake_minimum_required(VERSION 3.22)

# Single source of truth for plugin/project SemVer.
set(PX3_VERSION "0.7.0")

if (NOT PX3_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR "PX3_VERSION must use SemVer MAJOR.MINOR.PATCH (example: 0.1.0).")
endif()

project(PX3Synth VERSION ${PX3_VERSION} LANGUAGES C CXX)

# Without this, a plain `cmake -S . -B build` leaves CMAKE_BUILD_TYPE empty on
# single-config generators, which means no optimisation flags at all: JUCE's
# recommended config flags only add -O3 for Release. A synth built that way is
# several times slower than the one that ships, and nothing in the output says
# so. Only applied when the caller has not chosen a configuration.
if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build configuration" FORCE)
    message(STATUS "PX3: CMAKE_BUILD_TYPE not set; defaulting to Release")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(PX3_COPY_PLUGIN_AFTER_BUILD "Copy plugin formats into local plugin folders after building" ON)
option(PX3_DEBUG_PANEL "Enable in-plugin debug panel UI" OFF)
# Temporary offline signal-path isolation harness. OFF for normal plugin builds.
option(PX3_BUILD_DIAGNOSTIC "Build the offline PX3Diag signal-path harness" OFF)

include(FetchContent)

# Reproducible JUCE dependency via FetchContent.
FetchContent_Declare(
    juce
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.4
)

set(FETCHCONTENT_UPDATES_DISCONNECTED ON)
FetchContent_MakeAvailable(juce)

# App icon. Generated from Source/Assets/px3.gif by scripts/make-app-icon.mjs
# and committed, so an ordinary build does not need node or sharp. Passed to
# JUCE only when it is actually present, so a fresh checkout without it still
# configures.
set(PX3_ICON_FILE "${CMAKE_SOURCE_DIR}/Source/Assets/px3-icon.png" CACHE FILEPATH
    "PNG used as the application/plugin icon")
set(PX3_ICON_ARGS "")
if (EXISTS "${PX3_ICON_FILE}")
    set(PX3_ICON_ARGS ICON_BIG "${PX3_ICON_FILE}")
    message(STATUS "PX3 app icon: ${PX3_ICON_FILE}")
else()
    message(STATUS "PX3 app icon: none (run scripts/make-app-icon.mjs to generate one)")
endif()

juce_add_plugin(PX3Synth
    ${PX3_ICON_ARGS}
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
        Source/Assets/ppp.png
        Source/Assets/px3.gif
)

set(PX3_SHARED_SOURCES
        Source/UI/PerformanceControls.cpp
        Source/Core/GlobalSettings.cpp
        Source/UI/PluginEditorLook.cpp
        Source/UI/PluginEditorFxCards.cpp
        Source/UI/PluginEditorMidiMacros.cpp
        Source/UI/PluginEditorPresets.cpp
        Source/UI/PluginEditorRefresh.cpp
        Source/UI/PluginEditorView.cpp
        Source/UI/PluginEditorConfig.cpp
        Source/UI/MacroStrip.cpp
        Source/UI/SettingsPanel.cpp
        Source/UI/KnobOverlays.cpp
        Source/UI/MacroKnobLook.cpp
        Source/UI/PianoKeyboard.cpp
        Source/UI/OscPanel.cpp
        Source/UI/ModPanel.cpp
        Source/UI/AmpEnvelopeComponent.cpp
        Source/UI/AmpPanel.cpp
        Source/UI/FltPanel.cpp
        Source/UI/FxPanel.cpp
        Source/UI/MixPanel.cpp
        Source/UI/ModalBackdrop.cpp
    Source/UI/RoundedRect.cpp
    Source/UI/VuMeterComponent.cpp
    Source/UI/BusInsertOverlay.cpp
    Source/UI/BusEqGraph.cpp
    Source/UI/FetPanelStyle.cpp
    Source/UI/MixerControls.cpp
        Source/UI/MixerChannelComponent.cpp
        Source/UI/TopMenuBar.cpp
        Source/UI/DelayComponent.cpp
        Source/UI/Card.cpp
        Source/DSP/CombResonator.cpp
        Source/DSP/StftEngine.cpp
        Source/DSP/AnalogEngine.cpp
        Source/DSP/Doom.cpp
        Source/DSP/Lucy.cpp
        Source/DSP/Chorus.cpp
        Source/DSP/ParametricEQ.cpp
        Source/DSP/FetCompressor.cpp
        Source/DSP/StereoSpread.cpp
        Source/UI/FxSignalFlow.cpp
        Source/UI/FxChainLayout.cpp
        Source/UI/BypassButton.cpp
        Source/UI/ToggleChipButton.cpp
        Source/UI/CardInner.cpp
        Source/UI/FxCardComponent.cpp
        Source/UI/MoodComponent.cpp
        Source/UI/EnvelopeComponent.cpp
        Source/UI/FilterComponent.cpp
        Source/UI/LfoComponent.cpp
        Source/UI/ReverbComponent.cpp
        Source/UI/UIConfig.cpp
        Source/UI/BreakpointEnvelopeEditor.cpp
        Source/UI/Wavetable3DRenderer.cpp
        Source/UI/WavetableGraph.cpp
        Source/UI/UIConfigManager.cpp
        Source/UI/SubOscComponent.cpp
        Source/UI/VibeComponent.cpp
        Source/UI/OscillatorComponent.cpp
        Source/UI/PluginEditor.cpp
        Source/UI/PluginEditorDebug.cpp
        Source/DSP/AmpEnvelope.cpp
        Source/DSP/EnvelopeGenerator.cpp
        Source/DSP/Delay.cpp
        Source/DSP/Mood.cpp
        Source/DSP/LfoGenerator.cpp
        Source/DSP/Reverb.cpp
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
        Source/DSP/BreakpointEnvelope.cpp
        Source/DSP/OscillatorUnit.cpp
        Source/DSP/Wavetable.cpp
        Source/DSP/WavetableFactory.cpp
        Source/DSP/WavetableImporter.cpp
        Source/DSP/WavetableLibrary.cpp
        Source/DSP/VoiceFilter.cpp
        Source/DSP/SynthVoice.cpp
        Source/DSP/PX3Diagnostics.cpp
        Source/Preset/PresetManager.cpp
        Source/Preset/FactoryPresets.cpp
)

set(PX3_INCLUDE_DIRS
        Source
        Source/UI
        Source/DSP
        Source/Preset
        Source/Core
)

target_sources(PX3Synth PRIVATE ${PX3_SHARED_SOURCES})

target_include_directories(PX3Synth PRIVATE ${PX3_INCLUDE_DIRS})

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
        juce::juce_opengl
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

# Offline signal-path isolation harness. Renders the real processor headlessly
# and reports per-stage sample-delta discontinuities. Temporary diagnostic tool.
if (PX3_BUILD_DIAGNOSTIC)
    juce_add_console_app(PX3Diag PRODUCT_NAME "PX3Diag")
    juce_generate_juce_header(PX3Diag)

    target_sources(PX3Diag PRIVATE ${PX3_SHARED_SOURCES} Source/Tools/DiagnosticMain.cpp)
    target_include_directories(PX3Diag PRIVATE ${PX3_INCLUDE_DIRS})

    target_compile_definitions(PX3Diag
        PRIVATE
            JUCE_WEB_BROWSER=0
            JUCE_USE_CURL=0
            JUCE_STANDALONE_APPLICATION=1
            PX3_DEBUG_PANEL=0
            PX3_DIAGNOSTICS=1
            JucePlugin_Name="PX3 Synth"
    )

    target_link_libraries(PX3Diag
        PRIVATE
            PX3Assets
            juce::juce_audio_utils
            juce::juce_dsp
            juce::juce_opengl
        PUBLIC
            juce::juce_recommended_config_flags
    )
endif()

# Factory-default audio smoke test, built in the SHIPPING configuration
# (PX3_DIAGNOSTICS off) so it exercises the code the plugin actually runs.
if (PX3_BUILD_DIAGNOSTIC)
    juce_add_console_app(PX3SmokeTest PRODUCT_NAME "PX3SmokeTest")
    juce_generate_juce_header(PX3SmokeTest)
    target_sources(PX3SmokeTest PRIVATE ${PX3_SHARED_SOURCES} Source/Tools/AudioSmokeTest.cpp)
    target_include_directories(PX3SmokeTest PRIVATE ${PX3_INCLUDE_DIRS})
    target_compile_definitions(PX3SmokeTest
        PRIVATE
            JUCE_WEB_BROWSER=0
            JUCE_USE_CURL=0
            JUCE_STANDALONE_APPLICATION=1
            PX3_DEBUG_PANEL=0
            PX3_DIAGNOSTICS=0
            JucePlugin_Name="PX3 Synth"
    )
    target_link_libraries(PX3SmokeTest
        PRIVATE PX3Assets juce::juce_audio_utils juce::juce_dsp juce::juce_opengl
        PUBLIC juce::juce_recommended_config_flags)
endif()

# Component regression / correctness tests, built in the SHIPPING configuration
# so they exercise the code the plugin actually runs.
if (PX3_BUILD_DIAGNOSTIC)
    juce_add_console_app(PX3Tests PRODUCT_NAME "PX3Tests")
    juce_generate_juce_header(PX3Tests)
    # One module per area rather than one 32,000-line translation unit. They
    # share Source/Tools/Tests/TestSupport.h for the counters and fixtures.
    file(GLOB PX3_TEST_SOURCES CONFIGURE_DEPENDS Source/Tools/Tests/*.cpp)
    target_sources(PX3Tests PRIVATE ${PX3_SHARED_SOURCES} ${PX3_TEST_SOURCES})
    target_include_directories(PX3Tests PRIVATE ${PX3_INCLUDE_DIRS})
    target_compile_definitions(PX3Tests
        PRIVATE
            JUCE_WEB_BROWSER=0
            JUCE_USE_CURL=0
            JUCE_STANDALONE_APPLICATION=1
            PX3_DEBUG_PANEL=0
            PX3_DIAGNOSTICS=0
            # The GPU check needs to pump the message loop to let a GL context
            # come up. Only the test tool - the plugin must never run a modal
            # loop inside a host.
            JUCE_MODAL_LOOPS_PERMITTED=1
            JucePlugin_Name="PX3 Synth"
    )
    target_link_libraries(PX3Tests
        PRIVATE PX3Assets juce::juce_audio_utils juce::juce_dsp juce::juce_opengl
        PUBLIC juce::juce_recommended_config_flags)
endif()

# Offline memory benchmark. Measures process-level memory against real
# processor instances, in the SHIPPING configuration, so the numbers describe
# the code that ships. Complements `PX3Diag memory`, which reports static
# per-object sizes and cannot see allocator overhead or fragmentation.
if (PX3_BUILD_DIAGNOSTIC)
    juce_add_console_app(PX3MemBench PRODUCT_NAME "PX3MemBench")
    juce_generate_juce_header(PX3MemBench)
    target_sources(PX3MemBench PRIVATE ${PX3_SHARED_SOURCES} Source/Tools/MemoryBenchmark.cpp)
    target_include_directories(PX3MemBench PRIVATE ${PX3_INCLUDE_DIRS})
    target_compile_definitions(PX3MemBench
        PRIVATE
            JUCE_WEB_BROWSER=0
            JUCE_USE_CURL=0
            JUCE_STANDALONE_APPLICATION=1
            PX3_DEBUG_PANEL=0
            PX3_DIAGNOSTICS=0
            JucePlugin_Name="PX3 Synth"
    )
    target_link_libraries(PX3MemBench
        PRIVATE PX3Assets juce::juce_audio_utils juce::juce_dsp juce::juce_opengl
        PUBLIC juce::juce_recommended_config_flags)
endif()

# CPU benchmark, built in the SHIPPING configuration (PX3_DIAGNOSTICS off) so it
# measures the code the plugin actually runs. The diagnostic taps in PX3Diag add
# per-sample work to the very loops being timed.
if (PX3_BUILD_DIAGNOSTIC)
    juce_add_console_app(PX3Bench PRODUCT_NAME "PX3Bench")
    juce_generate_juce_header(PX3Bench)
    target_sources(PX3Bench PRIVATE ${PX3_SHARED_SOURCES} Source/Tools/CpuBenchmark.cpp)
    target_include_directories(PX3Bench PRIVATE ${PX3_INCLUDE_DIRS})
    target_compile_definitions(PX3Bench
        PRIVATE
            JUCE_WEB_BROWSER=0
            JUCE_USE_CURL=0
            JUCE_STANDALONE_APPLICATION=1
            PX3_DEBUG_PANEL=0
            PX3_DIAGNOSTICS=0
            JucePlugin_Name="PX3 Synth"
    )
    target_link_libraries(PX3Bench
        PRIVATE PX3Assets juce::juce_audio_utils juce::juce_dsp juce::juce_opengl
        PUBLIC juce::juce_recommended_config_flags)
endif()

# Ship external UI config alongside built bundles for runtime hot-reloadable styling.
set(PX3_UI_CONFIG_FILE "${CMAKE_SOURCE_DIR}/Source/UI/UIConfig.json")
foreach(px3BundleTarget IN ITEMS PX3Synth_Standalone PX3Synth_AU PX3Synth_VST3)
    if (TARGET ${px3BundleTarget})
        add_custom_command(TARGET ${px3BundleTarget} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${px3BundleTarget}>/../Resources"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${PX3_UI_CONFIG_FILE}"
                "$<TARGET_FILE_DIR:${px3BundleTarget}>/../Resources/UIConfig.json"
            VERBATIM
        )
    endif()
endforeach()
