cmake_minimum_required(VERSION 3.22)

project(SynthProject VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(FetchContent)

# Reproducible JUCE dependency via FetchContent.
FetchContent_Declare(
    juce
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.4
)

set(FETCHCONTENT_UPDATES_DISCONNECTED ON)
FetchContent_MakeAvailable(juce)

juce_add_plugin(SynthProject
    COMPANY_NAME "PX3"
    BUNDLE_ID "com.px3.px3synth"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD TRUE
    PLUGIN_MANUFACTURER_CODE SyPr
    PLUGIN_CODE SyP1
    FORMATS AU VST3 Standalone
    PRODUCT_NAME "PX3 Synth"
)

juce_generate_juce_header(SynthProject)

juce_add_binary_data(PX3Assets
    SOURCES
        Source/pp.png
        Source/px3.gif
)

target_sources(SynthProject
    PRIVATE
        Source/PianoKeyboard.cpp
        Source/PluginEditor.cpp
        Source/PluginProcessor.cpp
        Source/SynthSound.cpp
        Source/SynthVoice.cpp
)

target_include_directories(SynthProject
    PRIVATE
        Source
)

target_compile_definitions(SynthProject
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
)

target_link_libraries(SynthProject
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
            SynthProject
            SynthProject_Standalone
            SynthProject_AU
            SynthProject_VST3)
        if (TARGET ${px3Target})
            target_compile_options(${px3Target} PRIVATE ${PX3_WARNING_OPTIONS})
        endif()
    endforeach()
endif()
