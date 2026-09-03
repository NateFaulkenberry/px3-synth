cmake_minimum_required(VERSION 3.22)

# Single source of truth for plugin/project SemVer.
set(PX3_VERSION "0.7.1")

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
include(${CMAKE_CURRENT_LIST_DIR}/cmake/PX3Product.cmake)

set(PX3_ICON_FILE "${CMAKE_SOURCE_DIR}/products/PX3Synth/Assets/px3-icon.png" CACHE FILEPATH
    "PNG used as the application/plugin icon")
set(PX3_ICON_ARGS "")
if (EXISTS "${PX3_ICON_FILE}")
    set(PX3_ICON_ARGS ICON_BIG "${PX3_ICON_FILE}")
    message(STATUS "PX3 app icon: ${PX3_ICON_FILE}")
else()
    message(STATUS "PX3 app icon: none (run scripts/make-app-icon.mjs to generate one)")
endif()

# Code every PX3 product can use. Compiled INTO each product rather than
# linked as a static library, deliberately: JuceHeader.h is generated per
# target and carries that target's JucePlugin_* identity, so one library built
# against one product's header would bake that product's identity into every
# other. Splitting the list is what gives the boundary; a shared library would
# give a wrong one.
set(PX3_SHARED_SOURCES
        shared/Infrastructure/Settings/GlobalSettings.cpp
        shared/UI/Components/PianoKeyboard.cpp
        shared/UI/Components/ModalBackdrop.cpp
    shared/UI/Components/RoundedRect.cpp
        shared/UI/Components/VuMeterComponent.cpp
    shared/UI/Style/FetPanelStyle.cpp
        shared/UI/Fx/DelayComponent.cpp
        shared/UI/Components/Card.cpp
        shared/DSP/Filter/CombResonator.cpp
        shared/DSP/Analog/AnalogEngine.cpp
        shared/DSP/Core/StftEngine.cpp
        shared/DSP/Doom/Doom.cpp
        shared/DSP/Lucy/Lucy.cpp
        shared/DSP/Chorus/Chorus.cpp
        shared/DSP/Core/FetCompressor.cpp
        shared/DSP/StereoSpread/StereoSpread.cpp
        shared/UI/Fx/FxSignalFlow.cpp
        shared/UI/Fx/FxChainLayout.cpp
        shared/UI/Components/BypassButton.cpp
        shared/UI/Components/ToggleChipButton.cpp
        shared/UI/Components/CardInner.cpp
        shared/UI/Fx/FxCardComponent.cpp
        shared/UI/Fx/MoodComponent.cpp
        shared/UI/Fx/FilterComponent.cpp
        shared/UI/Fx/ReverbComponent.cpp
        shared/UI/Style/UIConfig.cpp
        shared/UI/Style/KnobLookAndFeel.cpp
        shared/UI/Style/KnobOverlays.cpp
        shared/UI/Style/MacroKnobLook.cpp
        shared/UI/Style/UIConfigManager.cpp
        shared/UI/Fx/VibeComponent.cpp
        shared/DSP/Delay/Delay.cpp
        shared/DSP/Mood/Mood.cpp
        shared/DSP/Reverb/Reverb.cpp
        shared/DSP/Vibe/Vibe.cpp
        shared/DSP/Vibe/VibeEngine.cpp
        shared/DSP/Filter/VoiceFilter.cpp
        shared/Infrastructure/Update/SemanticVersion.cpp
        shared/Infrastructure/Update/UpdateModel.cpp
        shared/Infrastructure/Update/GitHubReleaseProvider.cpp
        shared/Infrastructure/Update/ProductRegistry.cpp
        shared/Infrastructure/Update/UpdateService.cpp
)

# PX3 Synth's own code. Nothing here is reusable by another product, and
# nothing above it is specific to this one.
set(PX3_SYNTH_SOURCES
        products/PX3Synth/UI/PerformanceControls.cpp
        products/PX3Synth/UI/PluginEditorPaint.cpp
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
        products/PX3Synth/UI/OscPanel.cpp
        products/PX3Synth/UI/ModPanel.cpp
        products/PX3Synth/UI/AmpEnvelopeComponent.cpp
        products/PX3Synth/UI/AmpPanel.cpp
        products/PX3Synth/UI/FltPanel.cpp
        products/PX3Synth/UI/FxPanel.cpp
        products/PX3Synth/UI/MixPanel.cpp
    products/PX3Synth/UI/BusInsertOverlay.cpp
    products/PX3Synth/UI/BusEqGraph.cpp
    products/PX3Synth/UI/MixerControls.cpp
        products/PX3Synth/UI/MixerChannelComponent.cpp
        products/PX3Synth/UI/TopMenuBar.cpp
        products/PX3Synth/DSP/ParametricEQ.cpp
        products/PX3Synth/UI/EnvelopeComponent.cpp
        products/PX3Synth/UI/LfoComponent.cpp
        products/PX3Synth/UI/BreakpointEnvelopeEditor.cpp
        products/PX3Synth/UI/Wavetable3DRenderer.cpp
        products/PX3Synth/UI/WavetableGraph.cpp
        products/PX3Synth/UI/SubOscComponent.cpp
        products/PX3Synth/UI/OscillatorComponent.cpp
        products/PX3Synth/UI/PluginEditor.cpp
        products/PX3Synth/UI/PluginEditorDebug.cpp
        products/PX3Synth/DSP/AmpEnvelope.cpp
        products/PX3Synth/DSP/EnvelopeGenerator.cpp
        products/PX3Synth/DSP/LfoGenerator.cpp
        products/PX3Synth/DSP/SubOscillator.cpp
        products/PX3Synth/DSP/PluginProcessor.cpp
        products/PX3Synth/DSP/PluginProcessorParameters.cpp
        products/PX3Synth/DSP/PluginProcessorMidi.cpp
        products/PX3Synth/DSP/PluginProcessorDebug.cpp
        products/PX3Synth/DSP/PluginProcessorSource.cpp
        products/PX3Synth/DSP/PluginProcessorEffects.cpp
        products/PX3Synth/DSP/PluginProcessorState.cpp
        products/PX3Synth/DSP/SynthSound.cpp
        products/PX3Synth/DSP/BreakpointEnvelope.cpp
        products/PX3Synth/DSP/OscillatorUnit.cpp
        products/PX3Synth/DSP/Wavetable.cpp
        products/PX3Synth/DSP/WavetableFactory.cpp
        products/PX3Synth/DSP/WavetableImporter.cpp
        products/PX3Synth/DSP/WavetableLibrary.cpp
        products/PX3Synth/DSP/SynthVoice.cpp
        products/PX3Synth/DSP/PX3Diagnostics.cpp
        products/PX3Synth/Preset/PresetManager.cpp
        products/PX3Synth/Preset/FactoryPresets.cpp
)

# Everything the Synth target compiles. Named so the tools and test targets,
# which build the Synth in full, can ask for it by one name.
# PX3 Delay. The same Delay the Synth runs, with a host adapter instead of the
# Synth's mixer - see docs/ECOSYSTEM_ARCHITECTURE.md.
# Everything but the entry point, so the test binary can compile the product
# alongside the Synth without two definitions of createPluginFilter.
set(PX3_DELAY_SOURCES
        shared/Infrastructure/Fx/FxPluginProcessor.cpp
        products/PX3Delay/PluginProcessor.cpp
        products/PX3Delay/PluginEditor.cpp
)

set(PX3_LUCY_SOURCES
        shared/Infrastructure/Fx/FxPluginProcessor.cpp
        shared/Infrastructure/Fx/FxCardEditor.cpp
        products/PX3Lucy/PluginProcessor.cpp
        products/PX3Lucy/PluginEditor.cpp
)

set(PX3_LUCY_PLUGIN_SOURCES ${PX3_LUCY_SOURCES} products/PX3Lucy/PluginEntry.cpp)

set(PX3_DOOM_SOURCES
        shared/Infrastructure/Fx/FxPluginProcessor.cpp
        shared/Infrastructure/Fx/FxCardEditor.cpp
        products/PX3Doom/PluginProcessor.cpp
        products/PX3Doom/PluginEditor.cpp
)

set(PX3_DOOM_PLUGIN_SOURCES ${PX3_DOOM_SOURCES} products/PX3Doom/PluginEntry.cpp)

set(PX3_REVERB_SOURCES
        shared/Infrastructure/Fx/FxPluginProcessor.cpp
        shared/Infrastructure/Fx/FxCardEditor.cpp
        products/PX3Reverb/PluginProcessor.cpp
        products/PX3Reverb/PluginEditor.cpp
)

set(PX3_REVERB_PLUGIN_SOURCES ${PX3_REVERB_SOURCES} products/PX3Reverb/PluginEntry.cpp)

set(PX3_SPREAD_SOURCES
        shared/Infrastructure/Fx/FxPluginProcessor.cpp
        shared/Infrastructure/Fx/FxCardEditor.cpp
        products/PX3Spread/PluginProcessor.cpp
        products/PX3Spread/PluginEditor.cpp
)

set(PX3_SPREAD_PLUGIN_SOURCES ${PX3_SPREAD_SOURCES} products/PX3Spread/PluginEntry.cpp)

set(PX3_CHORUS_SOURCES
        shared/Infrastructure/Fx/FxPluginProcessor.cpp
        shared/Infrastructure/Fx/FxCardEditor.cpp
        products/PX3Chorus/PluginProcessor.cpp
        products/PX3Chorus/PluginEditor.cpp
)

set(PX3_CHORUS_PLUGIN_SOURCES ${PX3_CHORUS_SOURCES} products/PX3Chorus/PluginEntry.cpp)

set(PX3_MOOD_SOURCES
        shared/Infrastructure/Fx/FxPluginProcessor.cpp
        products/PX3Mood/PluginProcessor.cpp
        products/PX3Mood/PluginEditor.cpp
)

set(PX3_MOOD_PLUGIN_SOURCES ${PX3_MOOD_SOURCES} products/PX3Mood/PluginEntry.cpp)

set(PX3_DELAY_PLUGIN_SOURCES ${PX3_DELAY_SOURCES} products/PX3Delay/PluginEntry.cpp)

set(PX3_ALL_SOURCES ${PX3_SHARED_SOURCES} ${PX3_SYNTH_SOURCES})

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
        products/PX3Delay
        products/PX3Mood
        products/PX3Chorus
        products/PX3Spread
        products/PX3Reverb
        products/PX3Doom
        products/PX3Lucy
        shared/Infrastructure/Fx
)

# The warning policy, defined before any product so px3_add_product can apply
# it to every one of them.
#
# It used to be a foreach over four hand-written target names, below the product
# declarations. Seven products and the update helper were added above it and
# none of them inherited it - so -Wno-nontrivial-memcall reached 1 of the 9
# targets that compile JUCE's bundled harfbuzz, and the other 8 each emitted
# ~22 warnings about third-party code on every clean build. A policy that has
# to be edited when a product is added is a policy that gets forgotten.
if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    # The warnings we want everywhere. Old enough to be universal.
    set(PX3_WARNING_OPTIONS
        -Wall
        -Wextra
        -Wpedantic
        -Wshadow
        -Wconversion
        -Wsign-conversion
    )

    # The suppressions, each added only if THIS compiler knows it.
    #
    # Passing an unknown -Wno-... is not silently ignored: clang answers with
    # -Wunknown-warning-option, once per translation unit, and that warning
    # carries no file path - so it survives any filter written in terms of
    # paths and looks exactly like a warning in our own code. Two of these
    # flags postdate the clang that ships with the CI runner's Xcode, which is
    # how a policy meant to keep the build quiet became the thing making noise.
    #
    # A suppression is only needed where the warning it suppresses exists, so
    # dropping the unknown ones costs nothing: a compiler that has never heard
    # of -Wnontrivial-memcall will not emit it either.
    #
    # check_cxx_compiler_flag is reliable for the -Wno- form here because CMake
    # adds -Werror=unknown-warning-option for Clang, turning the probe into a
    # hard error rather than the warning clang would otherwise defer.
    include(CheckCXXCompilerFlag)
    foreach(px3Suppression
            # Third-party, in JUCE's vendored harfbuzz: it memcpys its own
            # non-trivially-copyable types by design. Not ours to fix.
            -Wno-nontrivial-memcall
            -Wno-switch-enum
            -Wno-unused-but-set-variable
            -Wno-unnecessary-virtual-specifier)
        string(MAKE_C_IDENTIFIER "PX3_HAS${px3Suppression}" px3FlagVar)
        check_cxx_compiler_flag("${px3Suppression}" ${px3FlagVar})
        if (${px3FlagVar})
            list(APPEND PX3_WARNING_OPTIONS "${px3Suppression}")
        else()
            message(STATUS "PX3: compiler does not support ${px3Suppression}; skipping it")
        endif()
    endforeach()
endif()

# PX3 Synth, the first product. Declared exactly the way any other product is,
# so "how do I add PX3 Mood" has the same answer as "how was the Synth added".
#
# Its bundle id and plug-in code are grandfathered rather than following the
# newer convention: they are what is already installed on people's machines.
px3_add_product(
    TARGET       PX3Synth
    PRODUCT_NAME "PX3 Synth"
    BUNDLE_ID    "com.px3.px3synth"
    PLUGIN_CODE  SyP1
    IS_SYNTH     TRUE
    FORMATS      AU VST3 Standalone
    SOURCES      ${PX3_SYNTH_SOURCES})

px3_add_product(
    TARGET       PX3Delay
    PRODUCT_NAME "PX3 Delay"
    BUNDLE_ID    "com.px3.delay"
    PLUGIN_CODE  DlP1
    FORMATS      AU VST3
    SOURCES      ${PX3_DELAY_PLUGIN_SOURCES})

px3_add_product(
    TARGET       PX3Mood
    PRODUCT_NAME "PX3 Mood"
    BUNDLE_ID    "com.px3.mood"
    PLUGIN_CODE  MdP1
    FORMATS      AU VST3
    SOURCES      ${PX3_MOOD_PLUGIN_SOURCES})

px3_add_product(
    TARGET       PX3Chorus
    PRODUCT_NAME "PX3 Chorus"
    BUNDLE_ID    "com.px3.chorus"
    PLUGIN_CODE  ChP1
    FORMATS      AU VST3
    SOURCES      ${PX3_CHORUS_PLUGIN_SOURCES})

px3_add_product(
    TARGET       PX3Spread
    PRODUCT_NAME "PX3 Spread"
    BUNDLE_ID    "com.px3.spread"
    PLUGIN_CODE  SpP1
    FORMATS      AU VST3
    SOURCES      ${PX3_SPREAD_PLUGIN_SOURCES})

px3_add_product(
    TARGET       PX3Reverb
    PRODUCT_NAME "PX3 Reverb"
    BUNDLE_ID    "com.px3.reverb"
    PLUGIN_CODE  RvP1
    FORMATS      AU VST3
    SOURCES      ${PX3_REVERB_PLUGIN_SOURCES})

px3_add_product(
    TARGET       PX3Doom
    PRODUCT_NAME "PX3 Doom"
    BUNDLE_ID    "com.px3.doom"
    PLUGIN_CODE  DmP1
    FORMATS      AU VST3
    SOURCES      ${PX3_DOOM_PLUGIN_SOURCES})

px3_add_product(
    TARGET       PX3Lucy
    PRODUCT_NAME "PX3 Lucy"
    BUNDLE_ID    "com.px3.lucy"
    PLUGIN_CODE  LcP1
    FORMATS      AU VST3
    SOURCES      ${PX3_LUCY_PLUGIN_SOURCES})

juce_add_binary_data(PX3Assets
    SOURCES
        products/PX3Synth/Assets/ppp.png
        products/PX3Synth/Assets/px3.gif
)


# Offline signal-path isolation harness. Renders the real processor headlessly
# and reports per-stage sample-delta discontinuities. Temporary diagnostic tool.
if (PX3_BUILD_DIAGNOSTIC)
    juce_add_console_app(PX3Diag PRODUCT_NAME "PX3Diag")
    juce_generate_juce_header(PX3Diag)

    target_sources(PX3Diag PRIVATE ${PX3_ALL_SOURCES} tools/DiagnosticMain.cpp)
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
    target_sources(PX3SmokeTest PRIVATE ${PX3_ALL_SOURCES} tools/AudioSmokeTest.cpp)
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
    target_sources(PX3Tests PRIVATE ${PX3_ALL_SOURCES}
        shared/Infrastructure/Fx/FxPluginProcessor.cpp
        products/PX3Delay/PluginProcessor.cpp products/PX3Delay/PluginEditor.cpp
        products/PX3Mood/PluginProcessor.cpp products/PX3Mood/PluginEditor.cpp
        shared/Infrastructure/Fx/FxCardEditor.cpp
        products/PX3Chorus/PluginProcessor.cpp products/PX3Chorus/PluginEditor.cpp
        products/PX3Spread/PluginProcessor.cpp products/PX3Spread/PluginEditor.cpp
        products/PX3Reverb/PluginProcessor.cpp products/PX3Reverb/PluginEditor.cpp
        products/PX3Doom/PluginProcessor.cpp products/PX3Doom/PluginEditor.cpp
        products/PX3Lucy/PluginProcessor.cpp products/PX3Lucy/PluginEditor.cpp
        ${PX3_TEST_SOURCES})
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
    target_sources(PX3MemBench PRIVATE ${PX3_ALL_SOURCES} tools/MemoryBenchmark.cpp)
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
    target_sources(PX3Bench PRIVATE ${PX3_ALL_SOURCES} tools/CpuBenchmark.cpp)
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

# The console targets, which compile JUCE the same way the products do and so
# emit the same third-party warnings without the same suppressions. The products
# get this inside px3_add_product; these are not built by that function, so they
# are listed. Conditional targets are guarded rather than assumed.
foreach(px3ConsoleTarget IN ITEMS
        PX3Updater
        PX3Diag
        PX3SmokeTest
        PX3Tests
        PX3MemBench
        PX3Bench)
    if (TARGET ${px3ConsoleTarget} AND PX3_WARNING_OPTIONS)
        target_compile_options(${px3ConsoleTarget} PRIVATE ${PX3_WARNING_OPTIONS})
    endif()
endforeach()

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
