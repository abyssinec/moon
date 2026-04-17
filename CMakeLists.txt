cmake_minimum_required(VERSION 3.24)

project(MoonAudioEditor VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(MOON_ENABLE_DESKTOP "Build the desktop application" ON)
option(MOON_ENABLE_TRACKTION "Enable Tracktion Engine integration when available" OFF)
option(MOON_ENABLE_PACKAGING "Enable install rules and package metadata" ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(GNUInstallDirs)

find_package(JUCE CONFIG QUIET)

add_subdirectory(engine)

if (MOON_ENABLE_DESKTOP)
    add_subdirectory(ui)
    add_subdirectory(app)
endif()

if (MOON_ENABLE_PACKAGING)
    install(FILES README.md DESTINATION ${CMAKE_INSTALL_DOCDIR})
    install(DIRECTORY docs/ DESTINATION ${CMAKE_INSTALL_DOCDIR})
    install(
        DIRECTORY backend/
        DESTINATION backend
        FILES_MATCHING
            PATTERN "*.py"
            PATTERN "*.txt"
            PATTERN "*.bat"
            PATTERN "__pycache__" EXCLUDE
            PATTERN ".venv" EXCLUDE
    )
    include(Packaging)
endif()

message(STATUS "MoonAudioEditor configured")
message(STATUS "  Desktop: ${MOON_ENABLE_DESKTOP}")
message(STATUS "  Tracktion: ${MOON_ENABLE_TRACKTION}")
message(STATUS "  Packaging: ${MOON_ENABLE_PACKAGING}")


