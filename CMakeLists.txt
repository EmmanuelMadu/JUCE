cmake_minimum_required(VERSION 3.22)
project(HearthKeys VERSION 0.2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release)
endif()

# JUCE: use a local checkout when HK_JUCE_PATH is given, otherwise download the pinned release.
set(HK_JUCE_PATH "" CACHE PATH "Path to an existing JUCE checkout (optional)")
option(HK_LTO "Link-time optimisation (slower builds, slightly faster plug-in)" ON)
if(HK_JUCE_PATH)
    add_subdirectory(${HK_JUCE_PATH} juce EXCLUDE_FROM_ALL)
else()
    include(FetchContent)
    FetchContent_Declare(juce
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 9.0.5
        GIT_SHALLOW ON)
    FetchContent_MakeAvailable(juce)
endif()

if(APPLE)
    set(HK_FORMATS VST3 AU Standalone)
else()
    set(HK_FORMATS VST3 Standalone)
endif()

set(HK_DSP_SOURCES
    src/dsp/Engine.cpp
    src/dsp/GrandBank.cpp
    src/dsp/GrandPack.cpp
    src/dsp/GrandTable.cpp
    src/dsp/KeyTable.cpp
    src/dsp/StringBank.cpp
    src/dsp/TineBank.cpp)

juce_add_plugin(HearthKeys
    PRODUCT_NAME "Hearth Keys"
    COMPANY_NAME "Homemade"
    BUNDLE_ID "com.homemade.hearthkeys"
    PLUGIN_MANUFACTURER_CODE Hmde
    PLUGIN_CODE HrKy
    FORMATS ${HK_FORMATS}
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES Instrument Piano
    AU_MAIN_TYPE kAudioUnitType_MusicDevice
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(HearthKeys PRIVATE
    src/plugin/PluginProcessor.cpp
    src/plugin/PluginEditor.cpp
    ${HK_DSP_SOURCES})

target_compile_definitions(HearthKeys PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0)

target_link_libraries(HearthKeys
    PRIVATE juce::juce_audio_utils
    PUBLIC juce::juce_recommended_config_flags)
if(HK_LTO)
    target_link_libraries(HearthKeys PUBLIC juce::juce_recommended_lto_flags)
endif()

# Renders the interface to a PNG without a screen; used to check layouts on the build machine.
# It reuses the plugin's already-compiled code instead of building JUCE a second time.
add_executable(hk_snapshot tools/snapshot.cpp)
target_include_directories(hk_snapshot PRIVATE $<TARGET_PROPERTY:HearthKeys,INCLUDE_DIRECTORIES>)
target_compile_definitions(hk_snapshot PRIVATE $<TARGET_PROPERTY:HearthKeys,COMPILE_DEFINITIONS>)
target_link_libraries(hk_snapshot PRIVATE HearthKeys)

# The sound engine on its own (no JUCE): shared library for the Python tests.
add_library(hk SHARED ${HK_DSP_SOURCES} src/dsp/capi.cpp)
set_target_properties(hk PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(NOT MSVC)
    target_compile_options(hk PRIVATE -Wall -Wextra -Wshadow)
endif()
