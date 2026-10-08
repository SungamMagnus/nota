include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        9.0.1
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(JUCE)

option(NOTA_INSTALL_AFTER_BUILD "Copy the built VST3 into ~/Library/Audio/Plug-Ins/VST3" OFF)

# A VST3 *instrument* with an audio sidechain input. Ableton Live only routes a plug-in's MIDI
# output when the plug-in sits on a MIDI track, and only for VST (not AU): put Nota on a MIDI
# track, pick the audio source in its sidechain chooser, and any other track can take
# "MIDI From -> this track -> Nota".
#
# PLUGIN_CODE stays NotL so the VST3 class id is unchanged from the first "Nota Live" builds
# (Live identifies plug-ins by that id, so existing sets keep working).
juce_add_plugin(NotaVST
    COMPANY_NAME                "Sungam"
    BUNDLE_ID                   "com.sungam.nota"
    PLUGIN_MANUFACTURER_CODE    Sngm
    PLUGIN_CODE                 NotL
    FORMATS                     VST3
    PRODUCT_NAME                "Nota"
    DESCRIPTION                 "Monophonic audio to MIDI"
    IS_SYNTH                    TRUE
    NEEDS_MIDI_INPUT            TRUE # Live refuses a VST3 instrument without an event input bus
    NEEDS_MIDI_OUTPUT           TRUE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD     ${NOTA_INSTALL_AFTER_BUILD})

target_sources(NotaVST PRIVATE
    PluginProcessor.cpp
    PluginEditor.cpp
)

target_compile_definitions(NotaVST PUBLIC
    JUCE_ALSA=0
    JUCE_DIRECTSOUND=0
    JUCE_PLUGINHOST_VST3=0
    JUCE_USE_CURL=0
    JUCE_WEB_BROWSER=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(NotaVST
    PRIVATE
        nota_dsp
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
