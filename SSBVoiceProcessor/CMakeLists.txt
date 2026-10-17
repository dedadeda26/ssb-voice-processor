cmake_minimum_required(VERSION 3.22)

project(SSBVoiceProcessor VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT DEFINED JUCE_DIR)
    set(JUCE_DIR "D:/Dev/JUCE" CACHE PATH "Path to JUCE")
endif()

# ---- ASIO 检测 ----
set(JUCE_ASIO_FLAG 0)
set(ASIO_HEADER "${JUCE_DIR}/modules/juce_audio_devices/native/asio/common/asio.h")
if(EXISTS "${ASIO_HEADER}")
    set(JUCE_ASIO_FLAG 1)
    message(STATUS "ASIO SDK found: ASIO enabled")
else()
    message(STATUS "ASIO SDK not found: ASIO disabled.")
endif()

add_subdirectory(${JUCE_DIR} ${CMAKE_BINARY_DIR}/JUCE)

juce_add_gui_app(SSBVoiceProcessor
    PRODUCT_NAME "SSB Voice Processor"
    COMPANY_NAME "SSBVoice"
    VERSION "0.1.0"
    NEEDS_CURL FALSE
    NEEDS_WEB_BROWSER FALSE
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/Resources/icon.png"
)

juce_generate_juce_header(SSBVoiceProcessor)

target_sources(SSBVoiceProcessor PRIVATE
    Source/Main.cpp
    Source/SplashComponent.cpp
    Source/SplashComponent.h
    Source/TutorialWindow.cpp
    Source/TutorialWindow.h
    Source/icon_data.h
    Source/MainComponent.cpp
    Source/AudioEngine.cpp
    Source/Localization.cpp
    Source/Knob.cpp
    Source/LevelMeter.cpp
    Source/ModuleSelector.cpp
    Source/NodeCard.cpp
    Source/PreferencesDialog.cpp
    Source/PresetManager.cpp
    Source/AutoCalibrator.cpp
    Source/SmartTune.cpp
    Source/StylePresets.cpp
    Source/CalibrationOverlay.cpp
    Source/AppSettings.cpp
    Source/BackgroundImageCache.cpp
    Source/Nodes/InputGainNode.cpp
    Source/Nodes/HighPassNode.cpp
    Source/Nodes/NoiseGateNode.cpp
    Source/Nodes/SpectralNrNode.cpp
    Source/Nodes/EqNode.cpp
    Source/Nodes/MultiBandCompNode.cpp
    Source/Nodes/CompressorNode.cpp
    Source/Nodes/SaturationNode.cpp
    Source/Nodes/DeEsserNode.cpp
    Source/Nodes/LimiterNode.cpp
    Source/Nodes/AutoLevelNode.cpp
    Source/Nodes/BandLimitNode.cpp
    Source/Nodes/OutputGainNode.cpp
)

target_compile_definitions(SSBVoiceProcessor PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_ASIO=${JUCE_ASIO_FLAG}
    JUCE_WASAPI=1
    JUCE_DIRECTSOUND=1
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
    JUCE_APPLICATION_NAME_STRING="SSB Voice Processor"
    JUCE_APPLICATION_VERSION_STRING="0.1.0"
)

target_include_directories(SSBVoiceProcessor PRIVATE Source)

if(MSVC)
    target_compile_options(SSBVoiceProcessor PRIVATE /utf-8)

    # 默认以管理员身份运行：托盘图标注册与 HKCU\...\Run 写入在普通权限下会被
    # 系统以 ERROR_ACCESS_DENIED 拒绝，提权后两者都正常。
    target_link_options(SSBVoiceProcessor PRIVATE
        "/MANIFESTUAC:level='requireAdministrator' uiAccess='false'")
endif()

target_link_libraries(SSBVoiceProcessor PRIVATE
    juce::juce_audio_basics
    juce::juce_audio_devices
    juce::juce_audio_formats
    juce::juce_audio_utils
    juce::juce_core
    juce::juce_data_structures
    juce::juce_dsp
    juce::juce_events
    juce::juce_graphics
    juce::juce_gui_basics
    juce::juce_gui_extra
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
)

option(SSB_BUILD_TESTS "Build offline regression tests" OFF)
if(SSB_BUILD_TESTS)
    enable_testing()
    find_package(Threads REQUIRED)
    add_executable(ssb_display_queue_tests Tests/DisplayQueueTests.cpp)
    target_include_directories(ssb_display_queue_tests PRIVATE Source)
    target_link_libraries(ssb_display_queue_tests PRIVATE Threads::Threads)
    add_test(NAME display_queue COMMAND ssb_display_queue_tests)

    juce_add_console_app(ssb_priority_tests PRODUCT_NAME "SSB Priority Tests")
    target_sources(ssb_priority_tests PRIVATE
        Tests/PriorityFixes.cpp
        Tests/P2Fixes.cpp
        Tests/OptimizationTests.cpp
        Source/AudioEngine.cpp
        Source/AutoCalibrator.cpp
        Source/CalibrationOverlay.cpp
        Source/AppSettings.cpp
        Source/PresetManager.cpp
        Source/AutoCalibrator.cpp
        Source/SmartTune.cpp
        Source/MainComponent.cpp
        Source/TutorialWindow.cpp
        Source/PreferencesDialog.cpp
        Source/BackgroundImageCache.cpp
        Source/StylePresets.cpp
        Source/NodeCard.cpp
        Source/Knob.cpp
        Source/LevelMeter.cpp
        Source/ModuleSelector.cpp
        Source/Localization.cpp
        Source/Nodes/InputGainNode.cpp
        Source/Nodes/HighPassNode.cpp
        Source/Nodes/NoiseGateNode.cpp
    Source/Nodes/SpectralNrNode.cpp
        Source/Nodes/EqNode.cpp
        Source/Nodes/MultiBandCompNode.cpp
        Source/Nodes/CompressorNode.cpp
        Source/Nodes/SaturationNode.cpp
        Source/Nodes/DeEsserNode.cpp
        Source/Nodes/LimiterNode.cpp
    Source/Nodes/AutoLevelNode.cpp
        Source/Nodes/BandLimitNode.cpp
        Source/Nodes/OutputGainNode.cpp
    )
    target_include_directories(ssb_priority_tests PRIVATE Source)
    target_compile_definitions(ssb_priority_tests PRIVATE
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(ssb_priority_tests PRIVATE
        juce::juce_audio_devices juce::juce_dsp juce::juce_gui_basics juce::juce_data_structures)
    if(MSVC)
        target_compile_options(ssb_priority_tests PRIVATE /utf-8)
    endif()
    add_test(NAME priority_fixes COMMAND ssb_priority_tests)
    add_test(NAME p2_fixes COMMAND ssb_priority_tests --p2)
    add_test(NAME optimization COMMAND ssb_priority_tests --opt)
    set_tests_properties(display_queue priority_fixes p2_fixes optimization PROPERTIES TIMEOUT 120)
    if(APPLE)
        add_library(ssb_allocation_probe SHARED Tests/AllocationProbe.cpp)
        set_tests_properties(p2_fixes PROPERTIES
            ENVIRONMENT "DYLD_INSERT_LIBRARIES=$<TARGET_FILE:ssb_allocation_probe>")
    endif()
endif()
