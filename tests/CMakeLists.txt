include(FetchContent)

# Use the dynamic CRT so GoogleTest matches JUCE / the rest of the build (MSVC).
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

FetchContent_Declare(googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.14.0
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(googletest)

add_executable(GenVstTests
    SmokeTest.cpp)

target_include_directories(GenVstTests PRIVATE
    "${CMAKE_SOURCE_DIR}/src")

target_link_libraries(GenVstTests PRIVATE
    GTest::gtest_main
    juce::juce_core
    juce::juce_audio_basics)

include(GoogleTest)
gtest_discover_tests(GenVstTests)
