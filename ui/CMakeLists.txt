set(MOON_UI_SOURCES
    src/AIGenerationPanel.cpp
    src/TimelineView.cpp
    src/TrackListView.cpp
    src/InspectorPanel.cpp
    src/TransportBar.cpp
    src/TaskPanel.cpp
    src/WaveformClipView.cpp
    src/WaveformRenderer.cpp
    src/RegionSelectionOverlay.cpp
)
add_library(moon_ui STATIC ${MOON_UI_SOURCES})
target_include_directories(moon_ui PUBLIC include ../engine/include)

if (JUCE_FOUND)
    target_compile_definitions(moon_ui PUBLIC MOON_HAS_JUCE=1)
    target_link_libraries(moon_ui PUBLIC moon_engine juce::juce_gui_extra juce::juce_audio_utils juce::juce_opengl)
else()
    target_compile_definitions(moon_ui PUBLIC MOON_HAS_JUCE=0)
    target_link_libraries(moon_ui PUBLIC moon_engine)
endif()
