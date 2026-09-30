cmake_minimum_required(VERSION 3.22)

project(SpacenerdMaster VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JUCE: локальна копія (-DJUCE_LOCAL_PATH=...) або автозавантаження фіксованої версії
if(DEFINED JUCE_LOCAL_PATH)
    add_subdirectory(${JUCE_LOCAL_PATH} JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.15
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

if(APPLE)
    set(SN_FORMATS AU VST3)
else()
    set(SN_FORMATS VST3)
endif()

juce_add_plugin(SpacenerdMaster
    COMPANY_NAME             "Spacenerd"
    BUNDLE_ID                "space.spacenerd.master"
    PLUGIN_NAME              "Spacenerd Master"
    PRODUCT_NAME             "Spacenerd Master"
    DESCRIPTION              "Mastering chain: EQ, compressor, saturation, width, limiter"
    PLUGIN_MANUFACTURER_CODE Spcn
    PLUGIN_CODE              Mstr
    IS_SYNTH                 FALSE
    NEEDS_MIDI_INPUT         FALSE
    NEEDS_MIDI_OUTPUT        FALSE
    IS_MIDI_EFFECT           FALSE
    AU_MAIN_TYPE             kAudioUnitType_Effect
    VST3_CATEGORIES          Fx Mastering Dynamics
    COPY_PLUGIN_AFTER_BUILD  FALSE
    FORMATS                  ${SN_FORMATS})

set(SN_SOURCES
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_sources(SpacenerdMaster PRIVATE ${SN_SOURCES})

target_compile_definitions(SpacenerdMaster PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(SpacenerdMaster
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Офлайн-тест DSP (не потрапляє в плагін)
option(SN_BUILD_TESTS "Build offline DSP test" OFF)
if(SN_BUILD_TESTS)
    juce_add_console_app(SpacenerdMasterTest PRODUCT_NAME "SpacenerdMasterTest")
    target_sources(SpacenerdMasterTest PRIVATE ${SN_SOURCES} tests/OfflineTest.cpp)
    target_compile_definitions(SpacenerdMasterTest PRIVATE
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 SN_OFFLINE_TEST=1)
    target_link_libraries(SpacenerdMasterTest PRIVATE
        juce::juce_audio_utils juce::juce_dsp
        juce::juce_recommended_config_flags)
endif()
