@echo off
setlocal

set BUILD_DIR=build
set DIST_DIR=dist

if "%JUCE_DIR%"=="" (
    echo [Moon Audio Editor] JUCE_DIR is not set.
    echo Set JUCE_DIR to your local JUCE root before running this script.
    exit /b 1
)

if "%MOON_ENABLE_TRACKTION%"=="" (
    set MOON_ENABLE_TRACKTION=OFF
)

echo [Moon Audio Editor] Configuring Release build...
echo [Moon Audio Editor] Tracktion seam: %MOON_ENABLE_TRACKTION%
cmake -S . -B "%BUILD_DIR%" -DMOON_ENABLE_DESKTOP=ON -DMOON_ENABLE_PACKAGING=ON -DMOON_ENABLE_TRACKTION=%MOON_ENABLE_TRACKTION% -DJUCE_DIR="%JUCE_DIR%"
if errorlevel 1 exit /b 1

echo [Moon Audio Editor] Building Release...
cmake --build "%BUILD_DIR%" --config Release
if errorlevel 1 exit /b 1

echo [Moon Audio Editor] Installing to %DIST_DIR%...
cmake --install "%BUILD_DIR%" --config Release --prefix "%DIST_DIR%"
if errorlevel 1 exit /b 1

echo [Moon Audio Editor] Packaging release artifacts...
cpack --config "%BUILD_DIR%\CPackConfig.cmake" -C Release
if errorlevel 1 exit /b 1

echo [Moon Audio Editor] Release artifacts are ready.
echo [Moon Audio Editor] Build tree: %CD%\%BUILD_DIR%
echo [Moon Audio Editor] Installed tree: %CD%\%DIST_DIR%
echo [Moon Audio Editor] Next steps:
echo [Moon Audio Editor]   1. Start backend with backend\run_backend.bat
echo [Moon Audio Editor]   2. Launch moon_audio_editor.exe from build or dist
echo [Moon Audio Editor]   3. Follow docs\release_checklist.md and docs\windows_handoff.md
