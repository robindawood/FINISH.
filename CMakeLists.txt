cmake_minimum_required(VERSION 3.22)

# Universal binary (Intel + Apple Silicon) and minimum macOS version. Must be set before project().
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "macOS architectures")
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS version")

project(WHALE VERSION 1.0.0 LANGUAGES C CXX)

# Source files contain UTF-8 text (French UI strings).
if(MSVC)
    add_compile_options(/utf-8)
endif()

option(WHALE_COPY_AFTER_BUILD "Copy the plugin into the system plugin folder after building (needs admin rights on Windows)" OFF)
option(WHALE_BUILD_TESTS "Build the offline test runner" OFF)
set(WHALE_JUCE_DIR "" CACHE PATH "Path to a local JUCE checkout (leave empty to download JUCE 8 automatically)")

# Static MSVC runtime: the VST3 does not depend on the Visual C++ redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

if(WHALE_JUCE_DIR)
    add_subdirectory(${WHALE_JUCE_DIR} JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(juce
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.10
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(juce)
endif()

set(WHALE_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND WHALE_FORMATS AU)
endif()

juce_add_plugin(WHALE
    PRODUCT_NAME "WHALE"
    COMPANY_NAME "Whale Audio"
    BUNDLE_ID "com.whaleaudio.whale"
    VERSION ${PROJECT_VERSION}
    PLUGIN_MANUFACTURER_CODE Whla
    PLUGIN_CODE Whm1
    FORMATS ${WHALE_FORMATS}
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    VST3_CATEGORIES Fx Mastering
    AU_MAIN_TYPE kAudioUnitType_Effect
    COPY_PLUGIN_AFTER_BUILD ${WHALE_COPY_AFTER_BUILD})

juce_generate_juce_header(WHALE)

# Embedded fonts (SIL Open Font License, see Resources/Fonts).
juce_add_binary_data(WhaleAssets
    HEADER_NAME BinaryData.h
    NAMESPACE BinaryData
    SOURCES
        Resources/Fonts/Unbounded-SemiBold.ttf
        Resources/Fonts/Unbounded-Regular.ttf
        Resources/Fonts/Manrope-Medium.ttf
        Resources/Fonts/Manrope-Bold.ttf
        Resources/Fonts/JetBrainsMono-Medium.ttf)
set_target_properties(WhaleAssets PROPERTIES POSITION_INDEPENDENT_CODE ON)

set(WHALE_SOURCES
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/DSP/WhaleDSP.cpp
    Source/Analysis/AudioAnalyzer.cpp
    Source/Analysis/AnalysisSession.cpp
    Source/Auto/AutoMaster.cpp
    Source/Auto/SpectralCapture.cpp
    Source/Presets/Presets.cpp
    Source/UI/WhaleScene.cpp
    Source/UI/Widgets.cpp
    Source/UI/PresetBrowser.cpp
    Source/UI/ExpertPanel.cpp)

target_sources(WHALE PRIVATE ${WHALE_SOURCES})
target_compile_features(WHALE PRIVATE cxx_std_17)

set(WHALE_DEFINITIONS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_compile_definitions(WHALE PUBLIC ${WHALE_DEFINITIONS})

target_link_libraries(WHALE
    PRIVATE
        WhaleAssets
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# GPU-accelerated drawing of the animated whale (macOS / Windows).
if(APPLE OR WIN32)
    target_link_libraries(WHALE PRIVATE juce::juce_opengl)
endif()

if(WHALE_BUILD_TESTS)
    juce_add_console_app(WhaleTests PRODUCT_NAME "WhaleTests")
    juce_generate_juce_header(WhaleTests)
    target_sources(WhaleTests PRIVATE ${WHALE_SOURCES} Tests/TestMain.cpp)
    target_include_directories(WhaleTests PRIVATE Source)
    target_compile_features(WhaleTests PRIVATE cxx_std_17)
    target_compile_definitions(WhaleTests PRIVATE ${WHALE_DEFINITIONS} JucePlugin_Name="WHALE" JUCE_STANDALONE_APPLICATION=1 JUCE_MODAL_LOOPS_PERMITTED=1 WHALE_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    target_link_libraries(WhaleTests PRIVATE
        WhaleAssets juce::juce_audio_utils juce::juce_dsp
        juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
endif()
