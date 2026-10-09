cmake_minimum_required(VERSION 3.22)

# Universal binary (Intel + Apple Silicon) and minimum macOS version. Must be set before project().
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "macOS architectures")
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS version")

project(FINISH VERSION 1.2.0 LANGUAGES C CXX)

option(FINISH_COPY_AFTER_BUILD "Copy the plugin into the system plugin folder after building (needs admin rights on Windows)" OFF)
option(FINISH_BUILD_TESTS "Build the offline test runner" OFF)
set(FINISH_JUCE_DIR "" CACHE PATH "Path to a local JUCE checkout (leave empty to download JUCE 8 automatically)")

# Static MSVC runtime: the VST3 does not depend on the Visual C++ redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

if(FINISH_JUCE_DIR)
    add_subdirectory(${FINISH_JUCE_DIR} JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(juce
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.10
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(juce)
endif()

set(FINISH_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND FINISH_FORMATS AU)
endif()

juce_add_plugin(FINISH
    PRODUCT_NAME "FINISH"
    COMPANY_NAME "FINISH Audio"
    BUNDLE_ID "com.finishaudio.finish"
    VERSION ${PROJECT_VERSION}
    PLUGIN_MANUFACTURER_CODE Fnsh
    PLUGIN_CODE Fini
    FORMATS ${FINISH_FORMATS}
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    VST3_CATEGORIES Fx Mastering Analyzer
    AU_MAIN_TYPE kAudioUnitType_Effect
    COPY_PLUGIN_AFTER_BUILD ${FINISH_COPY_AFTER_BUILD})

juce_generate_juce_header(FINISH)

# Embedded fonts (SIL Open Font License, see Resources/Fonts).
juce_add_binary_data(FinishAssets
    HEADER_NAME BinaryData.h
    NAMESPACE BinaryData
    SOURCES
        Resources/Fonts/Unbounded-SemiBold.ttf
        Resources/Fonts/Unbounded-Regular.ttf
        Resources/Fonts/Manrope-Medium.ttf
        Resources/Fonts/Manrope-Bold.ttf
        Resources/Fonts/JetBrainsMono-Medium.ttf)
set_target_properties(FinishAssets PROPERTIES POSITION_INDEPENDENT_CODE ON)

set(FINISH_SOURCES
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/DSP/FinishDSP.cpp
    Source/Analysis/AudioAnalyzer.cpp
    Source/Analysis/AnalysisSession.cpp
    Source/Music/SectionDetector.cpp
    Source/Music/SectionFeatureAnalyzer.cpp
    Source/Music/TransitionAnalyzer.cpp
    Source/Music/SectionDiagnosisEngine.cpp
    Source/Music/VariantGenerator.cpp
    Source/Music/TrackEngine.cpp
    Source/Diagnosis/DiagnosisEngine.cpp
    Source/Intent/IntentParser.cpp
    Source/Decision/DecisionEngine.cpp
    Source/UI/TrackMapView.cpp
    Source/UI/Inspector.cpp
    Source/UI/Panels.cpp)

target_sources(FINISH PRIVATE ${FINISH_SOURCES})
target_compile_features(FINISH PRIVATE cxx_std_17)

set(FINISH_DEFINITIONS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_USE_MP3AUDIOFORMAT=1
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_compile_definitions(FINISH PUBLIC ${FINISH_DEFINITIONS})

target_link_libraries(FINISH
    PRIVATE
        FinishAssets
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

if(FINISH_BUILD_TESTS)
    juce_add_console_app(FinishTests PRODUCT_NAME "FinishTests")
    juce_generate_juce_header(FinishTests)
    target_sources(FinishTests PRIVATE ${FINISH_SOURCES} Tests/TestMain.cpp)
    target_include_directories(FinishTests PRIVATE Source)
    target_compile_features(FinishTests PRIVATE cxx_std_17)
    target_compile_definitions(FinishTests PRIVATE ${FINISH_DEFINITIONS} JucePlugin_Name="FINISH" JUCE_STANDALONE_APPLICATION=1 JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(FinishTests PRIVATE
        FinishAssets juce::juce_audio_utils juce::juce_dsp
        juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
endif()
