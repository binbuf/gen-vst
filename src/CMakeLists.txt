juce_add_plugin(GenVst
    COMPANY_NAME                "YourStudio"
    COMPANY_WEBSITE             "https://example.com"
    PLUGIN_MANUFACTURER_CODE    GnVs
    PLUGIN_CODE                 Genv
    FORMATS                     VST3 AU Standalone
    PRODUCT_NAME                "Gen VST"
    IS_SYNTH                    TRUE
    NEEDS_MIDI_INPUT            TRUE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    NEEDS_WEBVIEW2              TRUE
    AU_MAIN_TYPE                kAudioUnitType_MusicDevice
    AU_SANDBOX_SAFE             TRUE
    COPY_PLUGIN_AFTER_BUILD     ${COPY_PLUGIN_AFTER_BUILD}
    VST3_CATEGORIES             "Instrument|Synth"
    DESCRIPTION                 "Sega Genesis YM2612+SN76489 emulation"
    BUNDLE_ID                   "com.yourstudio.genvst"
    VERSION                     "0.1.0")

# --- Standalone patch directory (per-platform runtime data dir) ---------------
if(WIN32)
    set(GENVST_STANDALONE_PATCH_DIR "$ENV{LOCALAPPDATA}/GenVst/patches")
elseif(APPLE)
    set(GENVST_STANDALONE_PATCH_DIR "$ENV{HOME}/Library/Application Support/GenVst/patches")
else()
    set(GENVST_STANDALONE_PATCH_DIR "$ENV{HOME}/.local/share/GenVst/patches")
endif()

# --- Factory-patch delivery (ADR-0005) ----------------------------------------
# Factory patches ship as loose .tfi files, not embedded binary data. Enumerate
# ONLY the top-level .tfi in extern/patches/ — a recursive glob would pull in the
# gitignored extra/ developer test set (ADR-0004).
file(GLOB FACTORY_PATCHES "${CMAKE_SOURCE_DIR}/extern/patches/*.tfi")

# Stage into a clean dir, then copy that tree into each plugin bundle's Resources.
set(FACTORY_STAGE "${CMAKE_BINARY_DIR}/factory-patches")
file(MAKE_DIRECTORY "${FACTORY_STAGE}")
file(COPY ${FACTORY_PATCHES} DESTINATION "${FACTORY_STAGE}")
juce_add_bundle_resources_directory(GenVst "${FACTORY_STAGE}")

# Standalone has no bundle — install the factory patches to the data directory.
install(FILES ${FACTORY_PATCHES} DESTINATION "${GENVST_STANDALONE_PATCH_DIR}")
# ------------------------------------------------------------------------------

target_sources(GenVst PRIVATE
    PluginProcessor.cpp  PluginProcessor.h
    PluginEditor.cpp     PluginEditor.h)

target_compile_definitions(GenVst PUBLIC
    JUCE_WEB_BROWSER=1
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1
    $<$<BOOL:${GENVST_DEV_SERVER}>:GENVST_DEV_SERVER=1>)

target_link_libraries(GenVst PRIVATE
    juce::juce_audio_basics
    juce::juce_audio_devices
    juce::juce_audio_formats
    juce::juce_audio_plugin_client
    juce::juce_audio_processors
    juce::juce_audio_utils
    juce::juce_core
    juce::juce_data_structures
    juce::juce_events
    juce::juce_graphics
    juce::juce_gui_basics
    juce::juce_gui_extra
    juce::juce_dsp
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)
