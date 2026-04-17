# Windows Handoff / Передача Windows-сборки

## Goal / Цель
This guide is for the person who will build, package, and smoke-test Moon Audio Editor on a real Windows machine with JUCE, MSVC, and optional local AI runners.

Этот документ для человека, который будет собирать, упаковывать и проверять Moon Audio Editor на реальной Windows-машине с JUCE, MSVC и при необходимости локальными AI-раннерами.

## 1. Prerequisites / Предварительные условия
- Install `Visual Studio 2022` with `Desktop development with C++`.
- Install a recent Windows SDK.
- Ensure `cmake`, `cpack`, and optionally `NSIS` are available in `PATH`.
- Have a local JUCE checkout and know its absolute path.
- Optional: prepare local Demucs and/or ACE-Step runners before testing AI workflows.

## 2. Desktop Build / Сборка desktop
From the repository root:

```powershell
$env:JUCE_DIR="C:\path\to\JUCE"
.\package_release.bat
```

Optional Tracktion seam handoff:

```powershell
$env:JUCE_DIR="C:\path\to\JUCE"
$env:MOON_ENABLE_TRACKTION="ON"
.\package_release.bat
```

Expected result:
- `build\` contains the configured Release build tree.
- `dist\` contains the installed handoff tree.
- `cpack` produces a distributable archive, and on Windows with NSIS installed it can also produce an installer.

## 3. Backend Bring-Up / Подъем backend
Fallback-only quick start:

```powershell
cd backend
run_backend.bat
```

Real runner example:

```powershell
$env:MOON_DEMUCS_EXECUTABLE="python -m demucs"
$env:MOON_DEMUCS_TIMEOUT_SEC="1800"
$env:MOON_ACE_STEP_API_URL="http://127.0.0.1:8001"
$env:MOON_ACE_STEP_CHECKPOINT_PATH="C:\path\to\ace-step-checkpoint"
$env:MOON_ACE_STEP_GENERATE_TIMEOUT_SEC="600"
cd backend
run_backend.bat
```

Verify:
- `http://127.0.0.1:8000/health`
- `http://127.0.0.1:8000/models`

The response should show whether each runner is using `subprocess`, `api`, or `fallback`.

## 4. Desktop Smoke Test / Smoke test desktop
- Launch `moon_audio_editor.exe` from the build tree or installed `dist\` tree.
- Create a project and import a WAV file.
- Verify playback, waveform drawing, clip selection, region selection, and transport status.
- Run `Separate into stems`, `Rewrite selected region`, and `Add generated layer`.
- Watch `TaskPanel`, `logs\app.log`, and `/jobs/{id}` details if a runner falls back.
- Export mix, region, and stems.

## 5. Real Runner Validation / Проверка реальных раннеров
Demucs:
- Confirm the configured command is correct.
- Confirm the output directory contains expected stem WAV files.
- Confirm backend diagnostics show mapped source files or safe fallback copies.

ACE-Step:
- Confirm the local API responds on `/health` if using API mode.
- Confirm `/generate` returns an output path that really exists on disk.
- If using subprocess mode, confirm the command writes the expected WAV output path.

## 6. Packaging Handoff / Передача сборки
- Keep the generated archive/installer plus the `dist\` tree.
- Keep `README.md`, `docs\release_checklist.md`, and this file with the handoff.
- If something fails, capture:
  - `logs\app.log`
  - backend console output
  - `/health` and `/models` responses
  - the failing `/jobs/{id}` payload when possible
