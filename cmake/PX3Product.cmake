# Adding a PX3 product.
#
# One function so a new plugin is a call rather than a hundred lines copied
# from the Synth's target and then quietly diverging from it. Everything a
# product differs in - its name, its identifiers, its formats, its own sources -
# is an argument; everything products share is applied here once.
#
# The shared sources are compiled INTO each product rather than linked from a
# static library. That is deliberate: JuceHeader.h is generated per target and
# carries that target's JucePlugin_* identity, so a single library built
# against one product's header would bake that product's identity into every
# other one. The boundary comes from the source lists and the include paths.
#
#   px3_add_product(
#       TARGET        PX3Mood
#       PRODUCT_NAME  "PX3 Mood"
#       BUNDLE_ID     com.px3.mood
#       PLUGIN_CODE   MdP1          # unique per product, four characters
#       FORMATS       AU VST3       # Standalone only where a product wants one
#       IS_SYNTH      FALSE
#       SOURCES       ${PX3_MOOD_SOURCES})
#
# PLUGIN_CODE must be unique across the ecosystem: two products sharing one is
# how a DAW ends up loading the wrong plug-in.
function(px3_add_product)
    set(options "")
    set(oneValue TARGET PRODUCT_NAME BUNDLE_ID PLUGIN_CODE IS_SYNTH NEEDS_MIDI_INPUT)
    set(multiValue FORMATS SOURCES)
    cmake_parse_arguments(PX3P "${options}" "${oneValue}" "${multiValue}" ${ARGN})

    if (NOT PX3P_TARGET OR NOT PX3P_PRODUCT_NAME OR NOT PX3P_BUNDLE_ID OR NOT PX3P_PLUGIN_CODE)
        message(FATAL_ERROR "px3_add_product needs TARGET, PRODUCT_NAME, BUNDLE_ID and PLUGIN_CODE")
    endif()

    if (NOT PX3P_FORMATS)
        set(PX3P_FORMATS AU VST3)
    endif()
    if (NOT DEFINED PX3P_IS_SYNTH)
        set(PX3P_IS_SYNTH FALSE)
    endif()
    if (NOT DEFINED PX3P_NEEDS_MIDI_INPUT)
        set(PX3P_NEEDS_MIDI_INPUT ${PX3P_IS_SYNTH})
    endif()

    juce_add_plugin(${PX3P_TARGET}
        ${PX3_ICON_ARGS}
        COMPANY_NAME "PX3"
        BUNDLE_ID "${PX3P_BUNDLE_ID}"
        IS_SYNTH ${PX3P_IS_SYNTH}
        NEEDS_MIDI_INPUT ${PX3P_NEEDS_MIDI_INPUT}
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        COPY_PLUGIN_AFTER_BUILD FALSE
        # Shared across the ecosystem: one manufacturer, many products.
        PLUGIN_MANUFACTURER_CODE SyPr
        PLUGIN_CODE ${PX3P_PLUGIN_CODE}
        FORMATS ${PX3P_FORMATS}
        PRODUCT_NAME "${PX3P_PRODUCT_NAME}")

    juce_generate_juce_header(${PX3P_TARGET})

    target_sources(${PX3P_TARGET} PRIVATE ${PX3_SHARED_SOURCES} ${PX3P_SOURCES})
    target_include_directories(${PX3P_TARGET} PRIVATE ${PX3_INCLUDE_DIRS})

    target_compile_definitions(${PX3P_TARGET}
        PRIVATE
            JUCE_WEB_BROWSER=0
            JUCE_USE_CURL=0
            # Paint each dirty rectangle on its own (JUCE's Metal-layer path).
            # Without it macOS hands JUCE the BOUNDING BOX of every dirty
            # rectangle, so a key spark at the bottom and a wave display at
            # the top repainted the whole window: with animations on, a held
            # chord pinned the message thread at 99% (now ~40%; PX3Bench
            # uinative). JUCE documents the trade-off as app-specific.
            JUCE_COREGRAPHICS_RENDER_WITH_MULTIPLE_PAINT_CALLS=1
            PX3_DEBUG_PANEL=$<IF:$<BOOL:${PX3_DEBUG_PANEL}>,1,0>
            PX3_UI_DESIGNER=$<IF:$<BOOL:${PX3_UI_DESIGNER}>,1,0>)

    # The warning policy applies to the product and to each format target JUCE
    # generated for it. Doing this here rather than in a list of target names
    # means a product added tomorrow cannot silently opt out of it.
    if (PX3_WARNING_OPTIONS)
        target_compile_options(${PX3P_TARGET} PRIVATE ${PX3_WARNING_OPTIONS})
        foreach(px3Format IN LISTS PX3P_FORMATS)
            if (TARGET ${PX3P_TARGET}_${px3Format})
                target_compile_options(${PX3P_TARGET}_${px3Format}
                                       PRIVATE ${PX3_WARNING_OPTIONS})
            endif()
        endforeach()
    endif()

    # UIConfig.json, inside every bundle this product builds.
    #
    # A card-shaped effect styles itself from this file, and FxCardEditor finds
    # it by walking up from the running executable to a Resources folder. The
    # Synth's bundles were given a copy and the effects were not, so an
    # installed effect found nothing and fell back to code defaults - it looked
    # right in a development tree only because the search also probes the
    # repository, which is not there on a user's machine.
    #
    # Done here rather than in a list beside the Synth's, so a product added to
    # the table is styled without anyone remembering to add it twice.
    if (EXISTS "${CMAKE_SOURCE_DIR}/shared/UI/Style/UIConfig.json")
        # What the copy below depends on.
        #
        # A POST_BUILD command runs when the TARGET is built, and editing the
        # config does not make a target out of date - so the link step is
        # skipped, the copy never fires, and the bundle keeps the file it was
        # built with. LINK_DEPENDS makes these files inputs to the link, so
        # touching one relinks and the copy runs.
        set(PX3_LAYOUT_DEPENDS "")
        if (PX3P_IS_SYNTH)
            set(PX3_LAYOUT_DEPENDS "${CMAKE_SOURCE_DIR}/shared/UI/Style/InstrumentScene.json")
        endif()

        foreach(px3Format IN LISTS PX3P_FORMATS)
            set(PX3_LAYOUT_COPY_COMMANDS "")
            if (PX3P_IS_SYNTH)
                set(PX3_LAYOUT_COPY_COMMANDS
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${CMAKE_SOURCE_DIR}/shared/UI/Style/InstrumentScene.json"
                        "$<TARGET_FILE_DIR:${PX3P_TARGET}_${px3Format}>/../Resources/InstrumentScene.json")
            endif()

            if (TARGET ${PX3P_TARGET}_${px3Format})
                set_property(TARGET ${PX3P_TARGET}_${px3Format} APPEND PROPERTY
                    LINK_DEPENDS
                        "${CMAKE_SOURCE_DIR}/shared/UI/Style/UIConfig.json"
                        ${PX3_LAYOUT_DEPENDS})
                add_custom_command(TARGET ${PX3P_TARGET}_${px3Format} PRE_LINK
                    COMMAND ${CMAKE_COMMAND} -E make_directory
                        "$<TARGET_FILE_DIR:${PX3P_TARGET}_${px3Format}>/../Resources"
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${CMAKE_SOURCE_DIR}/shared/UI/Style/UIConfig.json"
                        "$<TARGET_FILE_DIR:${PX3P_TARGET}_${px3Format}>/../Resources/UIConfig.json"
                    ${PX3_LAYOUT_COPY_COMMANDS}
                    VERBATIM)
            endif()
        endforeach()
    endif()

    target_link_libraries(${PX3P_TARGET}
        PRIVATE
            PX3Assets
            juce::juce_audio_utils
            juce::juce_dsp
            juce::juce_cryptography
            juce::juce_opengl
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags)

    if (APPLE)
        foreach(px3Format IN LISTS PX3P_FORMATS)
            if (TARGET ${PX3P_TARGET}_${px3Format})
                add_custom_command(TARGET ${PX3P_TARGET}_${px3Format} POST_BUILD
                    COMMAND /usr/bin/codesign --force --sign - --timestamp=none
                        "$<TARGET_FILE_DIR:${PX3P_TARGET}_${px3Format}>/../.."
                    COMMENT "Seal ${PX3P_PRODUCT_NAME} ${px3Format} after bundle generation"
                    VERBATIM)
            endif()
        endforeach()
    endif()

    if (PX3_COPY_PLUGIN_AFTER_BUILD)
        juce_enable_copy_plugin_step(${PX3P_TARGET})
    endif()
endfunction()
