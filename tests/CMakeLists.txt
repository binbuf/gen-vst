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
    FrequencyCalcTests.cpp
    RegisterWriteTests.cpp
    VoiceAllocatorTests.cpp
    MidiRoutingTests.cpp
    # Units under test, compiled straight into the test binary.
    "${CMAKE_SOURCE_DIR}/src/PatchSystem.cpp"
    "${CMAKE_SOURCE_DIR}/src/FmRegisterMap.cpp"
    "${CMAKE_SOURCE_DIR}/src/PartManager.cpp"
    "${CMAKE_SOURCE_DIR}/src/MidiRouter.cpp"
    "${CMAKE_SOURCE_DIR}/src/Voice.cpp"
    "${CMAKE_SOURCE_DIR}/src/VoiceAllocator.cpp")

# The ymfm OPN2 core — VoiceAllocatorTests drives real ymfm::ym2612 instances
# through Voice. ymfm_opn.cpp also defines OPNA/OPNB, whose constructors pull in
# the SSG and ADPCM engines, so those translation units must be compiled too or
# the link fails with undefined symbols (mirrors src/CMakeLists.txt).
target_sources(GenVstTests PRIVATE
    "${CMAKE_SOURCE_DIR}/third_party/ymfm/src/ymfm_opn.cpp"
    "${CMAKE_SOURCE_DIR}/third_party/ymfm/src/ymfm_ssg.cpp"
    "${CMAKE_SOURCE_DIR}/third_party/ymfm/src/ymfm_adpcm.cpp"
    "${CMAKE_SOURCE_DIR}/third_party/ymfm/src/ymfm_misc.cpp")

target_include_directories(GenVstTests PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
    "${CMAKE_SOURCE_DIR}/third_party/ymfm/src")

# PatchLoaderTests loads every factory .tfi straight from the source tree.
target_compile_definitions(GenVstTests PRIVATE
    GENVST_FACTORY_PATCHES_DIR="${CMAKE_SOURCE_DIR}/extern/patches")

target_link_libraries(GenVstTests PRIVATE
    GTest::gtest_main
    juce::juce_core
    juce::juce_audio_basics)

include(GoogleTest)
gtest_discover_tests(GenVstTests)
