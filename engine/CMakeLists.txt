set(MOON_ENGINE_SOURCES
    src/ArrangementBackend.cpp
    src/EngineRuntimeCoordinator.cpp
    src/ProjectManager.cpp
    src/SessionSerializer.cpp
    src/TracktionSyncBridge.cpp
    src/TimelineFacade.cpp
    src/TransportFacade.cpp
    src/WaveformService.cpp
    src/WaveformAnalyzer.cpp
    src/WaveformDiskCache.cpp
    src/ClipOperations.cpp
    src/ExportService.cpp
    src/AIJobClient.cpp
    src/LocalJobClient.cpp
    src/ModelManager.cpp
    src/TaskManager.cpp
    src/Logger.cpp
    src/SettingsService.cpp
    src/FileHash.cpp
)
add_library(moon_engine STATIC ${MOON_ENGINE_SOURCES})
target_include_directories(moon_engine PUBLIC include)

if (JUCE_FOUND)
    target_compile_definitions(moon_engine PUBLIC MOON_HAS_JUCE=1)
    target_link_libraries(moon_engine PUBLIC juce::juce_audio_formats juce::juce_audio_utils)
else()
    target_compile_definitions(moon_engine PUBLIC MOON_HAS_JUCE=0)
endif()

if (MOON_ENABLE_TRACKTION)
    target_compile_definitions(moon_engine PUBLIC MOON_HAS_TRACKTION=1)
else()
    target_compile_definitions(moon_engine PUBLIC MOON_HAS_TRACKTION=0)
endif()

