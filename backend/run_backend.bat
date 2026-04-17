@echo off
setlocal

cd /d "%~dp0"

if not exist ".venv\Scripts\python.exe" (
    echo [Moon Audio Editor] Creating backend virtual environment...
    python -m venv .venv
    if errorlevel 1 exit /b 1
)

call ".venv\Scripts\activate.bat"
if errorlevel 1 exit /b 1

if not exist ".venv\installed.stamp" (
    echo [Moon Audio Editor] Installing backend requirements...
    python -m pip install -r requirements.txt
    if errorlevel 1 exit /b 1
    type nul > ".venv\installed.stamp"
)

echo [Moon Audio Editor] Starting local backend on http://127.0.0.1:8000
if not "%MOON_DEMUCS_EXECUTABLE%"=="" echo [Moon Audio Editor] Demucs command: %MOON_DEMUCS_EXECUTABLE%
if not "%MOON_DEMUCS_TIMEOUT_SEC%"=="" echo [Moon Audio Editor] Demucs timeout (sec): %MOON_DEMUCS_TIMEOUT_SEC%
if not "%MOON_ACE_STEP_API_URL%"=="" echo [Moon Audio Editor] ACE-Step API: %MOON_ACE_STEP_API_URL%
if not "%MOON_ACE_STEP_CHECKPOINT_PATH%"=="" echo [Moon Audio Editor] ACE-Step checkpoint: %MOON_ACE_STEP_CHECKPOINT_PATH%
if not "%MOON_ACE_STEP_GENERATE_TIMEOUT_SEC%"=="" echo [Moon Audio Editor] ACE-Step generate timeout (sec): %MOON_ACE_STEP_GENERATE_TIMEOUT_SEC%
python -m uvicorn main:app --host 127.0.0.1 --port 8000
