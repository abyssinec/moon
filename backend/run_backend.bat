@echo off
setlocal EnableExtensions EnableDelayedExpansion

set "BACKEND_ROOT=%~dp0"
if "%BACKEND_ROOT:~-1%"=="\" set "BACKEND_ROOT=%BACKEND_ROOT:~0,-1%"

set "PYTHON_EXE=%BACKEND_ROOT%\.venv\Scripts\python.exe"
if not exist "%PYTHON_EXE%" (
    echo [moon] python not found: %PYTHON_EXE%
    exit /b 1
)

set "VENV_SCRIPTS=%BACKEND_ROOT%\.venv\Scripts"
set "VENV_ROOT=%BACKEND_ROOT%\.venv"
set "VENV_LIB_BIN=%BACKEND_ROOT%\.venv\Library\bin"
set "TORCH_LIB=%BACKEND_ROOT%\.venv\Lib\site-packages\torch\lib"

set "PATH=%VENV_SCRIPTS%;%VENV_ROOT%;%VENV_LIB_BIN%;%TORCH_LIB%;%PATH%"

set "MOON_ACE_STEP_API_URL="
if exist "%BACKEND_ROOT%\..\models\ace-step" (
    set "MOON_ACE_STEP_CHECKPOINT_PATH=%BACKEND_ROOT%\..\models\ace-step"
)

if /I "%~1"=="--probe-cuda" (
    "%PYTHON_EXE%" -c "import json, torch; print(json.dumps({'torch_version': torch.__version__, 'cuda_version': torch.version.cuda, 'cuda_available': torch.cuda.is_available(), 'device_count': torch.cuda.device_count() if torch.cuda.is_available() else 0}, ensure_ascii=False))"
    exit /b %ERRORLEVEL%
)

set "PORT=8000"
if not "%~1"=="" set "PORT=%~1"

for /f "tokens=5" %%P in ('netstat -ano ^| findstr /R /C:":%PORT% .*LISTENING"') do (
    echo [moon] stopping stale backend pid %%P on port %PORT%
    taskkill /PID %%P /F >nul 2>nul
)

echo [moon] BACKEND_ROOT=%BACKEND_ROOT%
echo [moon] MOON_ACE_STEP_CHECKPOINT_PATH=%MOON_ACE_STEP_CHECKPOINT_PATH%
echo [moon] Starting uvicorn on 127.0.0.1:%PORT%

"%PYTHON_EXE%" -m uvicorn main:app --app-dir "%BACKEND_ROOT%" --host 127.0.0.1 --port %PORT%
exit /b %ERRORLEVEL%
