cmake_minimum_required(VERSION 3.22...4.2)

# macOS deployment target must be set before project()
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS deployment target")

project(FirstAir VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add JUCE
add_subdirectory(libs/JUCE)

# Add CLAP support
add_subdirectory(libs/clap-juce-extensions EXCLUDE_FROM_ALL)

# Faust include directory (for dsp.h, UI.h, MapUI.h, etc.)
set(FAUST_INCLUDE_DIR "/opt/homebrew/include" CACHE PATH "Faust include directory")

# Define the plugin
juce_add_plugin(FirstAir
    COMPANY_NAME "First Air"
    PLUGIN_MANUFACTURER_CODE FsAr
    PLUGIN_CODE Fair
    FORMATS AU VST3 Standalone
    PRODUCT_NAME "First Air"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD TRUE
    VST3_CATEGORIES "Fx" "Reverb"
    AU_MAIN_TYPE "kAudioUnitType_Effect"
)

# Add CLAP format
clap_juce_extensions_plugin(TARGET FirstAir
    CLAP_ID "com.firstair.reverb"
    CLAP_FEATURES "audio-effect" "reverb" "stereo"
)

# Binary resources (fonts, images, etc.)
juce_add_binary_data(FirstAirData
    SOURCES
        Resources/InstrumentSans-Variable.ttf
)

# Source files
target_sources(FirstAir
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
)

# Include directories
target_include_directories(FirstAir
    PRIVATE
        ${CMAKE_SOURCE_DIR}/generated    # Faust-generated DSP header
        ${CMAKE_SOURCE_DIR}/Source
        ${FAUST_INCLUDE_DIR}             # Faust runtime headers (dsp.h, UI.h, etc.)
)

# Preprocessor definitions
target_compile_definitions(FirstAir
    PUBLIC
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
)

# Link JUCE modules + binary data
target_link_libraries(FirstAir
    PRIVATE
        juce::juce_audio_utils
        juce::juce_gui_basics
        FirstAirData
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)
