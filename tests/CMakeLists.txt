include(FetchContent)

# Use the dynamic CRT so GoogleTest matches JUCE / the rest of the build (MSVC).
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

FetchContent_Declare(googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.14.0
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(googletest)

add_executable(GenVstTests
    SmokeTest.cpp
    PatchLoaderTests.cpp
    Y12LoaderTests.cpp
    OpmLoaderTests.cpp
    VgmExtractTests.cpp
    VgmLoggerTests.cpp
    PatchBrowserTests.cpp
    PluginStateTests.cpp
    PsgPresetTests.cpp
    FrequencyCalcTests.cpp
    RegisterWriteTests.cpp
    VoiceAllocatorTests.cpp
    PsgDacTests.cpp
    PsgEnvelopeTests.cpp
    AlgoMappingTests.cpp
    TuningTests.cpp
    DspDecimatorTests.cpp
    OutputFilterTests.cpp
    LadderEffectTests.cpp
    FmFreqCtrlModeTests.cpp
    FmLegatoTests.cpp
    FmTlInversionTests.cpp
    PluginStateRestoreOrderingTests.cpp
    # Units under test, compiled straight into the test binary.
    "${CMAKE_SOURCE_DIR}/src/PluginProcessor.cpp"   # GENVST_HEADLESS_TEST omits the WebView editor
    "${CMAKE_SOURCE_DIR}/src/Telemetry.cpp"
    "${CMAKE_SOURCE_DIR}/src/PatchSystem.cpp"
    "${CMAKE_SOURCE_DIR}/src/PatchBrowser.cpp"
    "${CMAKE_SOURCE_DIR}/src/PluginState.cpp"
    "${CMAKE_SOURCE_DIR}/src/PsgPreset.cpp"
    "${CMAKE_SOURCE_DIR}/src/VgmExtract.cpp"
    "${CMAKE_SOURCE_DIR}/src/VgmLogger.cpp"
    "${CMAKE_SOURCE_DIR}/src/FmRegisterMap.cpp"
    "${CMAKE_SOURCE_DIR}/src/Voice.cpp"
    "${CMAKE_SOURCE_DIR}/src/VoiceAllocator.cpp"
    "${CMAKE_SOURCE_DIR}/src/SN76489Wrapper.cpp"
    "${CMAKE_SOURCE_DIR}/src/SN76489Engine.cpp"
    "${CMAKE_SOURCE_DIR}/src/DspDecimator.cpp"
    "${CMAKE_SOURCE_DIR}/src/OutputFilter.cpp"
    "${CMAKE_SOURCE_DIR}/src/LadderEffect.cpp"
    "${CMAKE_SOURCE_DIR}/src/Tuning.cpp")

# The ymfm OPN2 core — VoiceAllocatorTests drives real ymfm::ym2612 instances
# through Voice. ymfm_opn.cpp also defines OPNA/OPNB, whose constructors pull in
# the SSG and ADPCM engines, so those translation units must be compiled too or
# the link fails with undefined symbols (mirrors src/CMakeLists.txt).
target_sources(GenVstTests PRIVATE
    "${CMAKE_SOURCE_DIR}/third_party/ymfm/src/ymfm_opn.cpp"
    "${CMAKE_SOURCE_DIR}/third_party/ymfm/src/ymfm_ssg.cpp"
    "${CMAKE_SOURCE_DIR}/third_party/ymfm/src/ymfm_adpcm.cpp"
    "${CMAKE_SOURCE_DIR}/third_party/ymfm/src/ymfm_misc.cpp")

# The libvgm SN76489 MAME core (and the logging helper it calls) — mirrors
# the plugin's libvgm subset (see src/CMakeLists.txt). PsgDacTests drives a
# real SN76489 chip through SN76489Wrapper.
set(LIBVGM_DIR "${CMAKE_SOURCE_DIR}/third_party/libvgm")
target_sources(GenVstTests PRIVATE
    "${LIBVGM_DIR}/emu/cores/sn76496.c"
    "${LIBVGM_DIR}/emu/logging.c")
set_source_files_properties(
    "${LIBVGM_DIR}/emu/cores/sn76496.c"
    "${LIBVGM_DIR}/emu/logging.c"
    PROPERTIES COMPILE_OPTIONS
        "$<$<C_COMPILER_ID:MSVC>:/w>$<$<NOT:$<C_COMPILER_ID:MSVC>>:-w>")

target_include_directories(GenVstTests PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
    "${CMAKE_SOURCE_DIR}/third_party/ymfm/src")

# PatchLoaderTests loads every factory .tfi straight from the source tree.
# Y12LoaderTests / OpmLoaderTests load on-disk Y12 + OPM fixtures from
# tests/fixtures/patches/ to exercise the full file-read path.
target_compile_definitions(GenVstTests PRIVATE
    GENVST_FACTORY_PATCHES_DIR="${CMAKE_SOURCE_DIR}/extern/patches"
    GENVST_FIXTURES_PATCHES_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures/patches"
    GENVST_FIXTURES_TUNINGS_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures/tunings"
    # Compile PluginProcessor.cpp without its WebView editor (createEditor
    # returns nullptr; the PluginEditor.h include is skipped) so the processor
    # can be instantiated in a headless gtest binary that links no GUI module.
    GENVST_HEADLESS_TEST=1
    # resolveFactoryRoot() falls back to GENVST_DEV_PATCH_DIR when the
    # executable-relative bundle walk fails (always, in a test binary), so the
    # cold-start default-load + drain have a real factory bank to act on — the
    # ordering tests depend on the FM/SQ defaults being loadable.
    GENVST_DEV_PATCH_DIR="${CMAKE_SOURCE_DIR}/extern/patches"
    # The ordering tests pump the message loop (runDispatchLoopUntil) to
    # dispatch the cold-start / mode_select AsyncUpdater. That API is gated
    # behind JUCE_MODAL_LOOPS_PERMITTED, which plugin builds disable — enable
    # it for the test binary only.
    JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0)

target_link_libraries(GenVstTests PRIVATE
    GTest::gtest_main
    juce::juce_core
    juce::juce_events           # PatchBrowser inherits juce::Thread
    juce::juce_audio_basics
    juce::juce_audio_formats
    juce::juce_audio_processors)   # PsgPreset uses AudioProcessorValueTreeState

include(GoogleTest)
# Default DISCOVERY_TIMEOUT is 5s — too tight for a freshly-linked Release
# binary on macOS (cold dyld + JUCE static deps push first-load past the
# limit and the POST_BUILD discovery step gets killed). 60s is the value
# CMake's own docs suggest for slow-to-load discovery binaries.
gtest_discover_tests(GenVstTests DISCOVERY_TIMEOUT 60)
