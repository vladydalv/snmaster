cmake_minimum_required(VERSION 3.22)

# Кожна збірка в CI отримує свою версію: Logic не тримає старий кеш AU
if(DEFINED ENV{GITHUB_RUN_NUMBER})
    set(SN_VERSION 0.3.$ENV{GITHUB_RUN_NUMBER})
else()
    set(SN_VERSION 0.3.0)
endif()
project(Spacenerd VERSION ${SN_VERSION})

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

set(SN_COMMON_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/Common/Widgets.cpp)

# sn_add_plugin(<target> <folder> <name> <plugin code> <description> <vst3 categories>)
function(sn_add_plugin target folder name code description categories)
    # Коротка назва для слота в DAW ("SN Master"); файл бандла лишається "Spacenerd Master"
    string(REPLACE "Spacenerd " "SN " short_name "${name}")
    juce_add_plugin(${target}
        COMPANY_NAME             "Spacenerd"
        BUNDLE_ID                "space.spacenerd.${folder}"
        PLUGIN_NAME              "${short_name}"
        PRODUCT_NAME             "${name}"
        DESCRIPTION              "${description}"
        PLUGIN_MANUFACTURER_CODE Spcn
        PLUGIN_CODE              ${code}
        IS_SYNTH                 FALSE
        NEEDS_MIDI_INPUT         FALSE
        NEEDS_MIDI_OUTPUT        FALSE
        IS_MIDI_EFFECT           FALSE
        AU_MAIN_TYPE             kAudioUnitType_Effect
        VST3_CATEGORIES          ${categories}
        COPY_PLUGIN_AFTER_BUILD  FALSE
        FORMATS                  ${SN_FORMATS})

    file(GLOB plugin_sources ${CMAKE_CURRENT_SOURCE_DIR}/${folder}/*.cpp)
    target_sources(${target} PRIVATE ${plugin_sources} ${SN_COMMON_SOURCES})
    target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Common)

    target_compile_definitions(${target} PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0)

    target_link_libraries(${target}
        PRIVATE
            juce::juce_audio_utils
            juce::juce_dsp
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)
endfunction()

sn_add_plugin(SpacenerdMaster Master "Spacenerd Master" Mstr
    "Mastering chain: EQ, compressor, saturation, width, true-peak limiter" "Fx Mastering Dynamics")

sn_add_plugin(SpacenerdTone Tone "Spacenerd Tone" Tone
    "Channel colour: transient shaper, tube, tape, exciter, de-esser" "Fx Distortion Dynamics")

sn_add_plugin(SpacenerdFeedback Feedback "Spacenerd Feedback" Fbck
    "Guitar amp feedback emulation: pitch-tracked resonant loop" "Fx Distortion")

sn_add_plugin(SpacenerdEra Era "Spacenerd Era" Erap
    "Decade-style mastering on an XY pixel display: year x intensity, genre-aware" "Fx Mastering")

sn_add_plugin(SpacenerdStomp Stomp "Spacenerd Stomp" Stmp
    "Pedal: circuit-morph drive/fuzz, tremolo/vibrato, tape echo" "Fx Distortion Modulation Delay")

# Офлайн-тести DSP (не потрапляють у плагіни)
option(SN_BUILD_TESTS "Build offline DSP tests" OFF)
if(SN_BUILD_TESTS)
    function(sn_add_test target folder testfile)
        juce_add_console_app(${target} PRODUCT_NAME "${target}")
        file(GLOB plugin_sources ${CMAKE_CURRENT_SOURCE_DIR}/${folder}/*.cpp)
        target_sources(${target} PRIVATE ${plugin_sources} ${SN_COMMON_SOURCES} tests/${testfile})
        target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Common)
        target_compile_definitions(${target} PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
        target_link_libraries(${target} PRIVATE juce::juce_audio_utils juce::juce_dsp
                                                juce::juce_recommended_config_flags)
    endfunction()
    sn_add_test(MasterTest Master MasterTest.cpp)
    sn_add_test(ToneTest   Tone   ToneTest.cpp)
    sn_add_test(FeedbackTest Feedback FeedbackTest.cpp)
    sn_add_test(MasterAudit Master MasterAudit.cpp)
    sn_add_test(FeedbackAudit Feedback FeedbackAudit.cpp)
    sn_add_test(ToneAudit Tone ToneAudit.cpp)
    sn_add_test(EraTest Era EraTest.cpp)
    sn_add_test(AssistTest Master AssistTest.cpp)
    sn_add_test(AnalyzerAccuracy Master AnalyzerAccuracy.cpp)
    sn_add_test(StompTest Stomp StompTest.cpp)
    sn_add_test(OctaveAudit Stomp OctaveAudit.cpp)
endif()
