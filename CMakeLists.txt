cmake_minimum_required(VERSION 3.22)

# Core Audio process taps (used to capture system output) need macOS 14.2+.
set(CMAKE_OSX_DEPLOYMENT_TARGET "14.2" CACHE STRING "Minimum macOS version")

project(SoundMeter VERSION 0.1.0 LANGUAGES C CXX)

if(APPLE)
    enable_language(OBJCXX)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(JUCE)

juce_add_gui_app(SoundMeter
    PRODUCT_NAME "Sound Meter"
    COMPANY_NAME "fermezag05"
    BUNDLE_ID "com.fermezag05.soundmeter"
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Sound Meter measures the level of the selected input device."
    PLIST_TO_MERGE "<plist><dict><key>NSAudioCaptureUsageDescription</key><string>Sound Meter measures the level of the audio your Mac is playing.</string></dict></plist>")

target_sources(SoundMeter PRIVATE
    Source/Main.cpp
    Source/MainComponent.cpp
    Source/LevelMeter.cpp
    Source/SpectrumAnalyzer.cpp)

if(APPLE)
    target_sources(SoundMeter PRIVATE Source/SystemAudioCapture_mac.mm)
    target_link_libraries(SoundMeter PRIVATE "-framework CoreAudio" "-framework Foundation")
else()
    target_sources(SoundMeter PRIVATE Source/SystemAudioCapture_stub.cpp)
endif()

target_compile_definitions(SoundMeter PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:SoundMeter,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:SoundMeter,JUCE_VERSION>")

target_link_libraries(SoundMeter
    PRIVATE
        juce::juce_audio_devices
        juce::juce_dsp
        juce::juce_gui_basics
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
