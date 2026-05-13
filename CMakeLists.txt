cmake_minimum_required(VERSION 3.22)

project(Rattler VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Allow pointing to a local JUCE checkout via -DJUCE_PATH=... for faster iteration.
# Falls back to FetchContent (downloads JUCE 8.0.12 from GitHub).
if(DEFINED JUCE_PATH)
    add_subdirectory("${JUCE_PATH}" juce)
else()
    include(FetchContent)
    FetchContent_Declare(
        JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.12
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(JUCE)
endif()

# AU is macOS-only; VST3 and Standalone work everywhere.
if(APPLE)
    set(RATTLER_FORMATS VST3 AU Standalone)
else()
    set(RATTLER_FORMATS VST3 Standalone)
endif()

juce_add_plugin(Rattler
    COMPANY_NAME "RinggTheory"
    PLUGIN_MANUFACTURER_CODE Ring
    PLUGIN_CODE Ratl
    FORMATS ${RATTLER_FORMATS}
    PRODUCT_NAME "Rattler"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD TRUE
    VST3_CATEGORIES Fx Dynamics
)

target_sources(Rattler
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
)

target_link_libraries(Rattler
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

target_compile_definitions(Rattler
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
)
