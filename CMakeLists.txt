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

# App icon. Generated from products/PX3Synth/Assets/px3.gif by scripts/make-app-icon.mjs
# and committed, so an ordinary build does not need node or sharp. Passed to
# JUCE only when it is actually present, so a fresh checkout without it still
# configures.
set(PX3_ICON_FILE "${CMAKE_SOURCE_DIR}/products/PX3Synth/Assets/px3-icon.png" CACHE FILEPATH
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
        products/PX3Synth/Assets/ppp.png
        products/PX3Synth/Assets/px3.gif
)

set(PX3_SHARED_SOURCES
        products/PX3Synth/UI/PerformanceControls.cpp
        shared/Infrastructure/Settings/GlobalSettings.cpp
        products/PX3Synth/UI/PluginEditorLook.cpp
        products/PX3Synth/UI/PluginEditorFxCards.cpp
        products/PX3Synth/UI/PluginEditorMidiMacros.cpp
        products/PX3Synth/UI/PluginEditorPresets.cpp
        products/PX3Synth/UI/PluginEditorRefresh.cpp
        products/PX3Synth/UI/PluginEditorView.cpp
        products/PX3Synth/UI/PluginEditorConfig.cpp
        products/PX3Synth/UI/PluginEditorBuild.cpp
        products/PX3Synth/UI/MacroStrip.cpp
        products/PX3Synth/UI/MacroDepthPanel.cpp
        products/PX3Synth/UI/SettingsPanel.cpp
        products/PX3Synth/UI/KnobOverlays.cpp
        products/PX3Synth/UI/MacroKnobLook.cpp
        shared/UI/Components/PianoKeyboard.cpp
        products/PX3Synth/UI/OscPanel.cpp
        products/PX3Synth/UI/ModPanel.cpp
        products/PX3Synth/UI/AmpEnvelopeComponent.cpp
        products/PX3Synth/UI/AmpPanel.cpp
        products/PX3Synth/UI/FltPanel.cpp
        products/PX3Synth/UI/FxPanel.cpp
        products/PX3Synth/UI/MixPanel.cpp
        shared/UI/Components/ModalBackdrop.cpp
    shared/UI/Components/RoundedRect.cpp
        shared/UI/Components/VuMeterComponent.cpp
    products/PX3Synth/UI/BusInsertOverlay.cpp
    products/PX3Synth/UI/BusEqGraph.cpp
    shared/UI/Style/FetPanelStyle.cpp
    products/PX3Synth/UI/MixerControls.cpp
        products/PX3Synth/UI/MixerChannelComponent.cpp
        products/PX3Synth/UI/TopMenuBar.cpp
        shared/UI/Fx/DelayComponent.cpp
        shared/UI/Components/Card.cpp
        shared/DSP/Filter/CombResonator.cpp
        products/PX3Synth/DSP/StftEngine.cpp
        shared/DSP/Analog/AnalogEngine.cpp
        shared/DSP/Doom/Doom.cpp
        shared/DSP/Lucy/Lucy.cpp
        shared/DSP/Chorus/Chorus.cpp
        products/PX3Synth/DSP/ParametricEQ.cpp
        shared/DSP/Core/FetCompressor.cpp
        shared/DSP/StereoSpread/StereoSpread.cpp
        shared/UI/Fx/FxSignalFlow.cpp
        shared/UI/Fx/FxChainLayout.cpp
        shared/UI/Components/BypassButton.cpp
        shared/UI/Components/ToggleChipButton.cpp
        shared/UI/Components/CardInner.cpp
        shared/UI/Fx/FxCardComponent.cpp
        shared/UI/Fx/MoodComponent.cpp
        products/PX3Synth/UI/EnvelopeComponent.cpp
        shared/UI/Fx/FilterComponent.cpp
        products/PX3Synth/UI/LfoComponent.cpp
        shared/UI/Fx/ReverbComponent.cpp
        shared/UI/Style/UIConfig.cpp
        products/PX3Synth/UI/BreakpointEnvelopeEditor.cpp
        products/PX3Synth/UI/Wavetable3DRenderer.cpp
        products/PX3Synth/UI/WavetableGraph.cpp
        shared/UI/Style/UIConfigManager.cpp
        products/PX3Synth/UI/SubOscComponent.cpp
        shared/UI/Fx/VibeComponent.cpp
        products/PX3Synth/UI/OscillatorComponent.cpp
        products/PX3Synth/UI/PluginEditor.cpp
        products/PX3Synth/UI/PluginEditorDebug.cpp
        products/PX3Synth/DSP/AmpEnvelope.cpp
        products/PX3Synth/DSP/EnvelopeGenerator.cpp
        shared/DSP/Delay/Delay.cpp
        shared/DSP/Mood/Mood.cpp
        products/PX3Synth/DSP/LfoGenerator.cpp
        shared/DSP/Reverb/Reverb.cpp
        products/PX3Synth/DSP/SubOscillator.cpp
        shared/DSP/Vibe/Vibe.cpp
        products/PX3Synth/DSP/PluginProcessor.cpp
        products/PX3Synth/DSP/PluginProcessorParameters.cpp
        products/PX3Synth/DSP/PluginProcessorMidi.cpp
        products/PX3Synth/DSP/PluginProcessorDebug.cpp
        products/PX3Synth/DSP/PluginProcessorSource.cpp
        products/PX3Synth/DSP/PluginProcessorEffects.cpp
        products/PX3Synth/DSP/PluginProcessorState.cpp
        products/PX3Synth/DSP/SynthSound.cpp
        shared/DSP/Vibe/VibeEngine.cpp
        products/PX3Synth/DSP/BreakpointEnvelope.cpp
        products/PX3Synth/DSP/OscillatorUnit.cpp
        products/PX3Synth/DSP/Wavetable.cpp
        products/PX3Synth/DSP/WavetableFactory.cpp
        products/PX3Synth/DSP/WavetableImporter.cpp
        products/PX3Synth/DSP/WavetableLibrary.cpp
        shared/DSP/Filter/VoiceFilter.cpp
        products/PX3Synth/DSP/SynthVoice.cpp
        products/PX3Synth/DSP/PX3Diagnostics.cpp
        shared/Infrastructure/Update/SemanticVersion.cpp
        shared/Infrastructure/Update/UpdateModel.cpp
        shared/Infrastructure/Update/GitHubReleaseProvider.cpp
        shared/Infrastructure/Update/ProductRegistry.cpp
        shared/Infrastructure/Update/UpdateService.cpp
        products/PX3Synth/Preset/PresetManager.cpp
        products/PX3Synth/Preset/FactoryPresets.cpp
)

set(PX3_INCLUDE_DIRS
        shared
        shared/DSP
        shared/DSP/Core
        shared/DSP/Mood
        shared/DSP/Delay
        shared/DSP/Reverb
        shared/DSP/Doom
        shared/DSP/Lucy
        shared/DSP/Chorus
        shared/DSP/StereoSpread
        shared/DSP/Vibe
        shared/DSP/Filter
        shared/DSP/Analog
        shared/UI
        shared/UI/Components
        shared/UI/Style
        shared/UI/Fx
        shared/Infrastructure
        shared/Infrastructure/Settings
        shared/Infrastructure/Update
        products/PX3Synth
        products/PX3Synth/DSP
        products/PX3Synth/UI
        products/PX3Synth/Preset
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
        juce::juce_cryptography
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

    target_sources(PX3Diag PRIVATE ${PX3_SHARED_SOURCES} tools/DiagnosticMain.cpp)
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
        juce::juce_cryptography
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
    target_sources(PX3SmokeTest PRIVATE ${PX3_SHARED_SOURCES} tools/AudioSmokeTest.cpp)
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
        PRIVATE PX3Assets juce::juce_audio_utils juce::juce_dsp juce::juce_cryptography juce::juce_opengl
        PUBLIC juce::juce_recommended_config_flags)
endif()

# The update helper. A real signed application, shipped beside the standalone
# by the same installer, so it inherits the same Developer ID and notarisation
# rather than being something the plugin downloads at runtime.
juce_add_console_app(PX3Updater PRODUCT_NAME "PX3 Updater")
juce_generate_juce_header(PX3Updater)
target_sources(PX3Updater PRIVATE
        updater/UpdaterMain.cpp
        shared/Infrastructure/Settings/GlobalSettings.cpp
        shared/Infrastructure/Update/SemanticVersion.cpp
        shared/Infrastructure/Update/UpdateModel.cpp
        shared/Infrastructure/Update/GitHubReleaseProvider.cpp
        shared/Infrastructure/Update/ProductRegistry.cpp
        shared/Infrastructure/Update/UpdateService.cpp)
target_include_directories(PX3Updater PRIVATE ${PX3_INCLUDE_DIRS})
target_compile_definitions(PX3Updater
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_STANDALONE_APPLICATION=1
        JucePlugin_Name="PX3 Synth")
target_link_libraries(PX3Updater
    PRIVATE juce::juce_gui_basics juce::juce_cryptography
    PUBLIC juce::juce_recommended_config_flags)

# Component regression / correctness tests, built in the SHIPPING configuration
# so they exercise the code the plugin actually runs.
if (PX3_BUILD_DIAGNOSTIC)
    juce_add_console_app(PX3Tests PRODUCT_NAME "PX3Tests")
    juce_generate_juce_header(PX3Tests)
    # One module per area rather than one 32,000-line translation unit. They
    # share tests/Tests/TestSupport.h for the counters and fixtures.
    file(GLOB PX3_TEST_SOURCES CONFIGURE_DEPENDS tests/Tests/*.cpp)
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
        PRIVATE PX3Assets juce::juce_audio_utils juce::juce_dsp juce::juce_cryptography juce::juce_opengl
        PUBLIC juce::juce_recommended_config_flags)
endif()

# Offline memory benchmark. Measures process-level memory against real
# processor instances, in the SHIPPING configuration, so the numbers describe
# the code that ships. Complements `PX3Diag memory`, which reports static
# per-object sizes and cannot see allocator overhead or fragmentation.
if (PX3_BUILD_DIAGNOSTIC)
    juce_add_console_app(PX3MemBench PRODUCT_NAME "PX3MemBench")
    juce_generate_juce_header(PX3MemBench)
    target_sources(PX3MemBench PRIVATE ${PX3_SHARED_SOURCES} tools/MemoryBenchmark.cpp)
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
        PRIVATE PX3Assets juce::juce_audio_utils juce::juce_dsp juce::juce_cryptography juce::juce_opengl
        PUBLIC juce::juce_recommended_config_flags)
endif()

# CPU benchmark, built in the SHIPPING configuration (PX3_DIAGNOSTICS off) so it
# measures the code the plugin actually runs. The diagnostic taps in PX3Diag add
# per-sample work to the very loops being timed.
if (PX3_BUILD_DIAGNOSTIC)
    juce_add_console_app(PX3Bench PRODUCT_NAME "PX3Bench")
    juce_generate_juce_header(PX3Bench)
    target_sources(PX3Bench PRIVATE ${PX3_SHARED_SOURCES} tools/CpuBenchmark.cpp)
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
        PRIVATE PX3Assets juce::juce_audio_utils juce::juce_dsp juce::juce_cryptography juce::juce_opengl
        PUBLIC juce::juce_recommended_config_flags)
endif()

# Ship external UI config alongside built bundles for runtime hot-reloadable styling.
set(PX3_UI_CONFIG_FILE "${CMAKE_SOURCE_DIR}/shared/UI/Style/UIConfig.json")
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
