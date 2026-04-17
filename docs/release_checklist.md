# Release Checklist / Чеклист релиза

## Windows Build / Сборка Windows
- Confirm `Visual Studio 2022`, `Desktop development with C++`, Windows SDK, and `JUCE_DIR` are available.
- Optional: set `MOON_ENABLE_TRACKTION=ON` before packaging if you want the Tracktion seam enabled in the handoff build.
- Run `package_release.bat` from the repository root.
- Verify that `dist/` contains the installed desktop app, `backend/`, `README.md`, and `docs/`.

## Backend Bring-Up / Подъем backend
- Run `backend\run_backend.bat`.
- If using real local runners, set `MOON_DEMUCS_EXECUTABLE`, `MOON_DEMUCS_TIMEOUT_SEC`, `MOON_ACE_STEP_API_URL`, `MOON_ACE_STEP_CHECKPOINT_PATH`, and optionally `MOON_ACE_STEP_GENERATE_TIMEOUT_SEC` before launch.
- Open `http://127.0.0.1:8000/health` and `http://127.0.0.1:8000/models`.
- Confirm runtime diagnostics show the intended `subprocess`, `api`, or `fallback` modes.
- Confirm `/jobs/{id}` details report sensible `stage`, `runner`, and `cache_hit` values during a real runner test.

## Desktop Smoke Test / Быстрый smoke test desktop
- Launch `moon_audio_editor.exe`.
- Create a new project and import a WAV file.
- Verify waveform drawing, play/pause/seek, timeline scrubbing, and region selection.
- Perform move, trim, split, fade, take activation, and crossfade edits.
- Save, close, reopen, and verify clips, generated assets, fades, and engine integration metadata restore correctly.

## AI Workflow Check / Проверка AI workflow
- Run `Separate into stems`, `Rewrite selected region`, and `Add generated layer`.
- Confirm `TaskPanel` shows progress and recent logs.
- Confirm identical reruns can report `cache hit`.
- For real Demucs runs, confirm the mapped stem outputs resolve under the cache directory and that missing stem files fall back safely without breaking job completion.
- For real ACE-Step runs, confirm the API or subprocess path writes the expected WAV and that timeout/error cases fall back to deterministic cached WAV outputs.
- If a real runner fails, verify the job still completes safely through fallback and that diagnostics explain the fallback reason.

## Export Check / Проверка экспорта
- Export mix, selected region, and stems.
- Confirm WAV files are audible.
- Confirm gain, fades, mute/solo, active takes, and same-track crossfades are reflected in the render.

## Packaging Handoff / Передача сборки
- Smoke-test the packaged output on a second Windows machine if available.
- Verify first-run notices, backend refresh, and startup recovery behavior.
- Keep `logs/app.log` from the handoff machine for quick diagnostics if anything fails.
