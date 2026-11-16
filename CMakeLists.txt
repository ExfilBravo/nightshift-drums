cmake_minimum_required(VERSION 3.22)

project(NightshiftDrums VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Static runtime so the plugin runs without the VC++ redistributable
if (MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# JUCE comes from the Nightshift checkout next door; override with -DJUCE_DIR=...
set(JUCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../nightshift/external/tracktion_engine/modules/juce" CACHE PATH "JUCE checkout")
add_subdirectory(${JUCE_DIR} juce)

# Per-user plugin folders, no admin rights needed: on Windows the VST3 goes to %LOCALAPPDATA%\Programs\Common\VST3
# (where the Lofi and Bass plugins already are); on a Mac to ~/Library/Audio/Plug-Ins, VST3 and Audio Units
if (APPLE)
    set(VST3_USER_DIR "$ENV{HOME}/Library/Audio/Plug-Ins/VST3")
    set(AU_USER_DIR "$ENV{HOME}/Library/Audio/Plug-Ins/Components")
    set(PLUGIN_FORMATS VST3 AU Standalone)
else()
    file(TO_CMAKE_PATH "$ENV{LOCALAPPDATA}/Programs/Common/VST3" VST3_USER_DIR)
    set(AU_USER_DIR "")
    set(PLUGIN_FORMATS VST3 Standalone)
endif()

# Everything but the two apps' own entry points: the kit engine, the grooves, the processor and its windows.
# Both the plugin and drumstool compile these (JUCE modules can't be shared through a static library).
file(GLOB DRUMS_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/common/*.cpp ${CMAKE_CURRENT_SOURCE_DIR}/common/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/*.cpp ${CMAKE_CURRENT_SOURCE_DIR}/engine/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/groove/*.cpp ${CMAKE_CURRENT_SOURCE_DIR}/groove/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/plugin/*.cpp ${CMAKE_CURRENT_SOURCE_DIR}/plugin/*.h)

set(DRUMS_DEFINITIONS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0   # built under JUCE's open-source licence
    JUCE_STRICT_REFCOUNTEDPOINTER=1)

# The kits' pictures and the drawn outlines of their pieces
file(GLOB DRUMS_ART CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/resources/kits/*)
juce_add_binary_data(DrumsArt SOURCES ${DRUMS_ART} NAMESPACE DrumsArt)

#==============================================================================
juce_add_plugin(NightshiftDrums
    PRODUCT_NAME "Nightshift Drums"
    COMPANY_NAME "Nightshift"
    BUNDLE_ID "com.nightshift.drums"
    PLUGIN_MANUFACTURER_CODE Nsft
    PLUGIN_CODE Ndrm
    FORMATS ${PLUGIN_FORMATS}
    VST3_CATEGORIES Instrument Drum
    AU_MAIN_TYPE kAudioUnitType_MusicDevice
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    COPY_PLUGIN_AFTER_BUILD TRUE
    VST3_COPY_DIR "${VST3_USER_DIR}"
    AU_COPY_DIR "${AU_USER_DIR}")

juce_generate_juce_header(NightshiftDrums)
target_sources(NightshiftDrums PRIVATE ${DRUMS_SOURCES})
target_include_directories(NightshiftDrums PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# JUCE_ASIO: the standalone can open the MiniFuse's ASIO driver (JUCE bundles the SDK)
target_compile_definitions(NightshiftDrums PUBLIC ${DRUMS_DEFINITIONS} JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1 $<$<PLATFORM_ID:Windows>:JUCE_ASIO=1>)
target_link_libraries(NightshiftDrums PRIVATE
    DrumsArt
    juce::juce_cryptography
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)

# The standalone is our own app: a plain window with audio and MIDI, or --snapshot for an offscreen picture
target_sources(NightshiftDrums_Standalone PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tools/standalone/DrumsStandalone.cpp)

if (MSVC)
    target_compile_options(NightshiftDrums PRIVATE /bigobj /MP)
    target_compile_options(NightshiftDrums_Standalone PRIVATE /bigobj /MP)
endif()

#==============================================================================
# drumstool: imports kits, renders, measures, and runs every test, on the same sources as the plugin
juce_add_console_app(drumstool
    PRODUCT_NAME "drumstool"
    COMPANY_NAME "Nightshift")

juce_generate_juce_header(drumstool)
file(GLOB DRUMSTOOL_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tools/drumstool/*.cpp ${CMAKE_CURRENT_SOURCE_DIR}/tools/drumstool/*.h)
target_sources(drumstool PRIVATE ${DRUMSTOOL_SOURCES} ${DRUMS_SOURCES})
target_include_directories(drumstool PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(drumstool PRIVATE
    ${DRUMS_DEFINITIONS}
    DRUMS_TOOL=1
    JUCE_PLUGINHOST_VST3=1
    JUCE_MODAL_LOOPS_PERMITTED=1
    JucePlugin_Name="Nightshift Drums"
    JucePlugin_VersionString="${PROJECT_VERSION}"
    JUCE_APPLICATION_NAME_STRING="drumstool"
    JUCE_APPLICATION_VERSION_STRING="${PROJECT_VERSION}")
target_link_libraries(drumstool PRIVATE
    DrumsArt
    juce::juce_cryptography
    juce::juce_audio_devices
    juce::juce_audio_processors
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

if (MSVC)
    target_compile_options(drumstool PRIVATE /bigobj /MP)
endif()
