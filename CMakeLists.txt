cmake_minimum_required(VERSION 3.22)
project(GenVst VERSION 0.2.1 LANGUAGES C CXX)   # C: libvgm SN76489 core sources

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(BUILD_TESTS "Build unit tests" ON)
option(COPY_PLUGIN_AFTER_BUILD "Copy plugin to system install directory" OFF)
option(GENVST_DEV_SERVER "Load the UI from the Vite dev server, not the embedded bundle" OFF)

# JUCE — fetched and pinned to a known-good tag.
include(FetchContent)
FetchContent_Declare(juce
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.4
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(juce)

# Code submodules must be initialized before configuring.
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/ymfm/src/ymfm.h")
    message(FATAL_ERROR "ymfm submodule not found. Run:\n  git submodule update --init --recursive")
endif()
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libvgm/emu/cores/sn76489.c")
    message(FATAL_ERROR "libvgm submodule not found. Run:\n  git submodule update --init --recursive")
endif()

if(APPLE)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64")
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15")   # ADR-0015 — verify vs JUCE 8 WebView
endif()

# WebView2 SDK — NEEDS_WEBVIEW2 makes JUCE's FindWebView2 require the
# Microsoft.Web.WebView2 NuGet package at configure time. Fetch and extract it
# so the build is self-contained (no manual NuGet install or CI prerequisite).
if(WIN32 AND NOT JUCE_WEBVIEW2_PACKAGE_LOCATION)
    set(webview2_version "1.0.1901.177")
    set(webview2_dir "${CMAKE_BINARY_DIR}/webview2")
    set(webview2_pkg "${webview2_dir}/Microsoft.Web.WebView2.${webview2_version}")

    if(NOT EXISTS "${webview2_pkg}/build/native/include/WebView2.h")
        message(STATUS "Fetching WebView2 SDK ${webview2_version}")
        set(webview2_zip "${CMAKE_BINARY_DIR}/webview2.zip")
        file(DOWNLOAD
            "https://www.nuget.org/api/v2/package/Microsoft.Web.WebView2/${webview2_version}"
            "${webview2_zip}"
            STATUS webview2_status)
        list(GET webview2_status 0 webview2_status_code)
        if(NOT webview2_status_code EQUAL 0)
            message(FATAL_ERROR "WebView2 SDK download failed: ${webview2_status}")
        endif()
        file(ARCHIVE_EXTRACT INPUT "${webview2_zip}" DESTINATION "${webview2_pkg}")
    endif()

    # Normal (non-cache) variable: JUCE predeclares JUCE_WEBVIEW2_PACKAGE_LOCATION
    # as a BOOL cache entry, so a non-FORCE cache set() here is a silent no-op.
    set(JUCE_WEBVIEW2_PACKAGE_LOCATION "${webview2_dir}")
endif()

add_subdirectory(src)

if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
