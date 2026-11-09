cmake_minimum_required(VERSION 3.22)
project(BassRack VERSION 0.4.1 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
include(CTest)
add_executable(bassrack_dsp_tests tests/DspTests.cpp)
target_include_directories(bassrack_dsp_tests PRIVATE Source)
add_test(NAME dsp COMMAND bassrack_dsp_tests)
option(BASSRACK_DSP_ONLY "Build portable DSP tests without JUCE" OFF)
if(BASSRACK_DSP_ONLY)
  return()
endif()
if(NOT APPLE)
  message(FATAL_ERROR "The plugin and AU hosting require macOS. Use -DBASSRACK_DSP_ONLY=ON elsewhere.")
endif()
include(FetchContent)
FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG 8.0.6 GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)
juce_add_plugin(BassRack
  COMPANY_NAME "Crimson Audio"
  BUNDLE_ID "audio.crimson.bassrack"
  PLUGIN_MANUFACTURER_CODE Crsn
  PLUGIN_CODE BsRk
  FORMATS AU Standalone
  PRODUCT_NAME "Bass Rack"
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT FALSE
  NEEDS_MIDI_OUTPUT FALSE
  IS_MIDI_EFFECT FALSE
  EDITOR_WANTS_KEYBOARD_FOCUS TRUE
  COPY_PLUGIN_AFTER_BUILD FALSE
  AU_MAIN_TYPE kAudioUnitType_Effect)
target_sources(BassRack PRIVATE Source/PluginProcessor.cpp Source/PluginEditor.cpp)
target_compile_definitions(BassRack PUBLIC
  JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
  JUCE_PLUGINHOST_AU=1 JUCE_PLUGINHOST_VST=0 JUCE_PLUGINHOST_VST3=0
  JUCE_DISPLAY_SPLASH_SCREEN=0)
target_link_libraries(BassRack PRIVATE
  juce::juce_audio_utils juce::juce_dsp
  PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

option(BASSRACK_BUILD_PREVIEW "Build native editor screenshot utility" OFF)
if(BASSRACK_BUILD_PREVIEW)
  juce_add_console_app(BassRackPreview PRODUCT_NAME "BassRackPreview")
  target_sources(BassRackPreview PRIVATE tools/CaptureEditor.cpp)
  target_link_libraries(BassRackPreview PRIVATE BassRack juce::juce_audio_utils juce::juce_dsp)
endif()
