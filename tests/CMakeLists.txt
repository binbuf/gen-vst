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
    # Units under test, compiled straight into the test binary.
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
    JUCE_WEB_BROWSER=0)

target_link_libraries(GenVstTests PRIVATE
    GTest::gtest_main
    juce::juce_core
    juce::juce_events           # PatchBrowser inherits juce::Thread
    juce::juce_audio_basics
    juce::juce_audio_formats
    juce::juce_audio_processors)   # PsgPreset uses AudioProcessorValueTreeState

include(GoogleTest)
gtest_discover_tests(GenVstTests)
