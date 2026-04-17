set(MOON_APP_SOURCES
    src/main.cpp
    src/MainWindow.cpp
    src/MainComponent.cpp
    src/AppController.cpp
)

set(MOON_APP_HEADERS
    include/AppConfig.h
    include/MainWindow.h
    include/MainComponent.h
    include/AppController.h
    include/CommandIDs.h
)

find_package(JUCE CONFIG QUIET)

if (JUCE_FOUND)
    add_executable(moon_audio_editor ${MOON_APP_SOURCES} ${MOON_APP_HEADERS})
    target_compile_definitions(moon_audio_editor PRIVATE MOON_HAS_JUCE=1)
    target_include_directories(moon_audio_editor PRIVATE include)
    target_link_libraries(moon_audio_editor
        PRIVATE
            moon_engine
            moon_ui
            juce::juce_gui_extra
            juce::juce_audio_utils
            juce::juce_audio_formats
    )
else()
    add_executable(moon_audio_editor src/main.cpp src/AppController.cpp ${MOON_APP_HEADERS})
    target_compile_definitions(moon_audio_editor PRIVATE MOON_HAS_JUCE=0)
    target_include_directories(moon_audio_editor PRIVATE include)
    target_link_libraries(moon_audio_editor PRIVATE moon_engine)
endif()

install(TARGETS moon_audio_editor
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
