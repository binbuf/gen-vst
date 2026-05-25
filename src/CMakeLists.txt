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
# Normalise to forward slashes so embedding the value as a C string literal in
# GENVST_STANDALONE_PATCH_DIR doesn't accidentally produce escape sequences
# (e.g. \U, \A) from a Windows LOCALAPPDATA path.
if(WIN32)
    file(TO_CMAKE_PATH "$ENV{LOCALAPPDATA}/GenVst/patches" GENVST_STANDALONE_PATCH_DIR)
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
    PluginProcessor.cpp   PluginProcessor.h
    PluginEditor.cpp      PluginEditor.h
    PluginState.cpp       PluginState.h
    Voice.cpp             Voice.h
    VoiceAllocator.cpp    VoiceAllocator.h
    FmRegisterMap.cpp     FmRegisterMap.h
    PatchSystem.cpp       PatchSystem.h
    PatchBrowser.cpp      PatchBrowser.h
    VgmExtract.cpp        VgmExtract.h
    VgmLogger.cpp         VgmLogger.h
    SN76489Wrapper.cpp    SN76489Wrapper.h
    SN76489Engine.cpp     SN76489Engine.h
    Telemetry.cpp         Telemetry.h
    Tuning.cpp            Tuning.h
    GenVstYmfmInterface.h)

# --- ymfm OPN2 (YM2612) FM core (ADR-0002) ------------------------------------
# Compiled inline into the plugin target rather than as a separate static lib,
# to avoid LTO boundary issues. ymfm_opn.cpp also defines the OPNA/OPNB chips
# (ym2608 / ym2610), whose constructors reference the SSG and ADPCM engines, so
# ymfm_ssg.cpp and ymfm_adpcm.cpp must be compiled as well or the link fails
# with undefined symbols.
target_sources(GenVst PRIVATE
    "${CMAKE_SOURCE_DIR}/third_party/ymfm/src/ymfm_opn.cpp"
    "${CMAKE_SOURCE_DIR}/third_party/ymfm/src/ymfm_ssg.cpp"
    "${CMAKE_SOURCE_DIR}/third_party/ymfm/src/ymfm_adpcm.cpp"
    "${CMAKE_SOURCE_DIR}/third_party/ymfm/src/ymfm_misc.cpp")
target_include_directories(GenVst PRIVATE
    "${CMAKE_SOURCE_DIR}/third_party/ymfm/src")
# ------------------------------------------------------------------------------

# --- libvgm SN76489 core (ADR-0009) ------------------------------------------
# Compile ONLY the MAME SN764xx core and the logging helper it depends on — not
# the rest of libvgm. The MAME variant supports the Sega VDP PSG via its config
# (taps 0x0009, 16-bit shift register, negate=1, segaPSG=1). All access goes
# through SN76489Wrapper; nothing else in the codebase touches libvgm.
set(LIBVGM_DIR "${CMAKE_SOURCE_DIR}/third_party/libvgm")
target_sources(GenVst PRIVATE
    "${LIBVGM_DIR}/emu/cores/sn76496.c"
    "${LIBVGM_DIR}/emu/logging.c")

# Silence the warnings JUCE's recommended flags raise against this third-party
# C code (mostly signed/unsigned conversions and zero-sized arrays in libvgm
# structs). The warnings are inherent to libvgm and not actionable here.
set_source_files_properties(
    "${LIBVGM_DIR}/emu/cores/sn76496.c"
    "${LIBVGM_DIR}/emu/logging.c"
    PROPERTIES COMPILE_OPTIONS
        "$<$<C_COMPILER_ID:MSVC>:/w>$<$<NOT:$<C_COMPILER_ID:MSVC>>:-w>")
# ------------------------------------------------------------------------------

target_compile_definitions(GenVst PUBLIC
    JUCE_WEB_BROWSER=1
    # NEEDS_WEBVIEW2 only links the SDK; this compile flag enables the WebView2
    # backend in the JUCE sources (and, via juce_gui_extra.h, JUCE_USE_WIN_WEBVIEW2
    # — which gates the resource provider). Windows-only; inert elsewhere.
    JUCE_USE_WIN_WEBVIEW2_WITH_STATIC_LINKING=1
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1
    $<$<BOOL:${GENVST_DEV_SERVER}>:GENVST_DEV_SERVER=1>)

# --- Factory-root runtime paths (Task 09 / ADR-0005) --------------------------
# PatchBrowser::resolveFactoryRoot uses these:
#   * Standalone reads from the platform data directory the install rule
#     populates.
#   * Plugin formats (VST3/AU) find the factory bank inside the bundle's
#     Contents/Resources/patches/ at runtime. The dev path is a fall-back so a
#     fresh `cmake --build` plays without an install step.
target_compile_definitions(GenVst PRIVATE
    GENVST_STANDALONE_PATCH_DIR="${GENVST_STANDALONE_PATCH_DIR}"
    GENVST_DEV_PATCH_DIR="${CMAKE_SOURCE_DIR}/extern/patches")
# ------------------------------------------------------------------------------

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
