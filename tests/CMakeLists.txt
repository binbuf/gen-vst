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
    # Units under test, compiled straight into the test binary. Both are pure
    # C++ (no JUCE, no ymfm), so they need no extra link libraries.
    "${CMAKE_SOURCE_DIR}/src/PatchSystem.cpp"
    "${CMAKE_SOURCE_DIR}/src/FmRegisterMap.cpp")

target_include_directories(GenVstTests PRIVATE
    "${CMAKE_SOURCE_DIR}/src")

# PatchLoaderTests loads every factory .tfi straight from the source tree.
target_compile_definitions(GenVstTests PRIVATE
    GENVST_FACTORY_PATCHES_DIR="${CMAKE_SOURCE_DIR}/extern/patches")

target_link_libraries(GenVstTests PRIVATE
    GTest::gtest_main
    juce::juce_core
    juce::juce_audio_basics)

include(GoogleTest)
gtest_discover_tests(GenVstTests)
