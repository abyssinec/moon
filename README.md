# Moon Audio Editor / Аудиоредактор Moon

## Overview / Обзор
Moon Audio Editor is a Windows-first, local-first desktop AI audio editor with a DAW-like workflow. The desktop side is built in C++20 and organized for JUCE + Tracktion Engine integration, while the local backend runs as FastAPI on `localhost` for offline-capable AI jobs.

Moon Audio Editor — это Windows-first, local-first desktop AI-аудиоредактор с DAW-подходом. Desktop-часть строится на C++20 и подготовлена под JUCE + Tracktion Engine, а локальный backend работает на FastAPI через `localhost` для офлайн-совместимых AI-задач.

## Current Status / Текущий статус
- Repository scaffold is in place.
- Desktop architecture is split into `app`, `engine`, and `ui`.
- Project create/open/save, autosave recovery, import, waveform drawing, selection, clip editing, tasks, logging, and backend job scaffolding are wired through the real app flow.
- Export now renders audible PCM WAV output from active clips instead of silent placeholders.
- Packaging/install metadata is now present for Windows-friendly `cmake --install` and `cpack` flows.

- Каркас репозитория собран.
- Desktop-архитектура разделена на `app`, `engine` и `ui`.
- Создание/открытие/сохранение проекта, autosave recovery, импорт, waveform, selection, clip editing, задачи, логи и backend job scaffolding уже подключены к реальному потоку приложения.
- Экспорт теперь рендерит слышимый PCM WAV из активных клипов вместо silent placeholder-файлов.
- Добавлены install/package-настройки для Windows-friendly сценариев через `cmake --install` и `cpack`.

## Build Instructions (Windows) / Сборка (Windows)
### Desktop
```powershell
cmake -S . -B build -DMOON_ENABLE_DESKTOP=ON -DMOON_ENABLE_PACKAGING=ON -DJUCE_DIR="C:/path/to/JUCE"
cmake --build build --config Release
```

Optional Tracktion seam build:
```powershell
cmake -S . -B build -DMOON_ENABLE_DESKTOP=ON -DMOON_ENABLE_PACKAGING=ON -DMOON_ENABLE_TRACKTION=ON -DJUCE_DIR="C:/path/to/JUCE"
cmake --build build --config Release
```

Optional install/package flow:
```powershell
cmake --install build --config Release --prefix dist
cpack --config build/CPackConfig.cmake -C Release
```

One-shot Windows packaging helper:
```powershell
$env:JUCE_DIR="C:\path\to\JUCE"
.\package_release.bat
```

Optional Tracktion seam packaging:
```powershell
$env:JUCE_DIR="C:\path\to\JUCE"
$env:MOON_ENABLE_TRACKTION="ON"
.\package_release.bat
```

Windows handoff guide:
- Use `docs/windows_handoff.md` for a step-by-step build/package/run handoff on a real Windows machine.

Requirements:
- Visual Studio 2022 with `Desktop development with C++`
- Windows SDK
- JUCE available through `JUCE_DIR` or a CMake package config
- NSIS optional if you want an `.exe` installer from `cpack`

Дополнительно для упаковки:
- `cmake --install` собирает install-tree с `moon_audio_editor.exe`, `backend/`, `README.md` и `docs/`
- `cpack` собирает переносимый архив, а на Windows при наличии NSIS — и установщик

### Backend
```powershell
cd backend
python -m venv .venv
.venv\Scripts\activate
pip install -r requirements.txt
uvicorn main:app --reload --host 127.0.0.1 --port 8000
```

Optional real local runner setup:
```powershell
$env:MOON_DEMUCS_EXECUTABLE="python -m demucs"
$env:MOON_DEMUCS_TIMEOUT_SEC="1800"
$env:MOON_ACE_STEP_API_URL="http://127.0.0.1:8001"
$env:MOON_ACE_STEP_CHECKPOINT_PATH="C:\path\to\ace-step-checkpoint"
$env:MOON_ACE_STEP_GENERATE_TIMEOUT_SEC="600"
backend\run_backend.bat
```

Expected local runner flow:
- Demucs: backend executes the configured command, looks for per-stem WAV files under the Demucs output directory, maps the discovered files to `vocals/drums/bass/other`, and safely falls back per stem if a file is missing.
- ACE-Step API: backend checks `GET /health`, then posts generation requests to `POST /generate`, accepts `output_audio_path`, `output_path`, or `audio_path` in the response, and falls back safely if the returned path is missing or the request fails.
- ACE-Step subprocess: backend invokes the configured command with input/output/prompt arguments and falls back if the process fails or does not write the expected WAV.
- In all cases, fallback remains deterministic: the backend still returns valid WAV outputs into cache paths instead of failing the whole desktop flow.

Windows convenience launcher:
```powershell
backend\run_backend.bat
```

## How To Verify / Как проверить
### Backend quick check
```powershell
curl http://127.0.0.1:8000/health
curl http://127.0.0.1:8000/models
```

Expected:
- `/health` returns `{"status":"ok","backend":"local-ai-audio-service"}`
- `/models` returns the current stems/rewrite/add-layer adapters
- `/health` and `/models` now also include additive runtime details showing whether Demucs / ACE-Step are using `subprocess`, `api`, or safe `fallback`
- repeat submissions with identical inputs/params now reuse cached outputs when available and report `cache hit`
- `/jobs/{id}` and `/jobs/{id}/result` now also include additive runner/cache details so local runner failures and fallback decisions are easier to diagnose
- `/jobs/{id}` details now also expose lightweight `stage` progress markers such as `preparing`, `processing`, `finalizing`, and `completed`

Ожидается:
- `/health` возвращает `{"status":"ok","backend":"local-ai-audio-service"}`
- `/models` возвращает текущие адаптеры stems/rewrite/add-layer

### Desktop quick check
1. Configure and build the app with JUCE available.
2. Launch `moon_audio_editor.exe` from `build`, or install/package it through `cmake --install` and `cpack`.
3. Use `New`, `Open`, `Save`, `Import WAV`, `Export Mix`, `Export Region`, `Export Stems`, or drag a `.wav` file into the window.
4. Confirm that:
- a clip appears on `Track 1`
- the clip width reflects file duration
- rough waveform is drawn
- clip selection works
- selected clip can be dragged horizontally and keeps its position after save/reopen
- region drag selection works
- timeline ruler supports click-and-drag scrubbing of the playhead
- transport time updates on play
- gain, trim, split, fade in/out, duplicate, delete, and take activation work from the inspector and shortcuts
- stems/rewrite/add-layer create tasks and insert generated clips
- reopening a saved project restores clips, generated assets, fades, take state, and waveform drawing
- `Settings` saves backend URL, cache directory, and default sample rate
- `TaskPanel` shows progress, logs, and writes `logs/app.log`
- `TaskPanel` also shows completed/failed task counts for quick diagnostics
- failed jobs surface a visible "Latest failure" banner in `TaskPanel` instead of getting lost in the log stream
- `Export Mix`, `Export Region`, and `Export Stems` render audible WAV content from active clips
- export respects clip gain, fade in/out, clip offsets, selected region bounds, active takes, and track mute/solo state
- track headers let you toggle `M` and `S`, and export/playback state follows those toggles
- when JUCE playback is available and project WAV assets match the live mix path requirements, `Play` now drives a live project mix source for the whole arrangement; cached preview remains the fallback path
- timeline scrubbing and playhead seeking now follow whole-project playback by default instead of jumping back to only the selected clip
- transport bar now includes a position scrubber tied to project playback duration instead of only a passive time label
- timeline navigation now supports viewport-based horizontal scrolling plus zoom controls/buttons and Ctrl+wheel zoom
- `Left`/`Right` nudge the playhead by `0.1s`, `Shift+Left`/`Shift+Right` by `1.0s`, `Home` seeks to zero, and `Escape` clears the selected region
- transport status explicitly shows whether the app is talking to the live localhost backend or using stub fallback
- startup shows a notice when the app restored from autosave or had to start in backend fallback mode
- the same startup notice also stays visible in the main window until you dismiss it, so recovery/fallback state is easy to verify after launch
- import/export/project actions now raise direct dialogs for common failures instead of relying only on the log panel
- `Refresh Backend` lets you re-check localhost backend health/models after starting FastAPI, without restarting the desktop app
- `Rebuild Preview` lets you regenerate the cached whole-timeline playback mix on demand after edits
- when transport is idle, the app now tries to auto-refresh stale project preview state in the background before the next playback
- the native window title now shows the current project name and a `*` marker when there are unsaved edits
- toolbar and inspector actions now enable only when their required clip/region/project state is available
- inspector hosting is now scrollable, so the full AI/edit control set remains reachable at normal window sizes
- the top bar now shows a live project summary with name, sample rate, track/clip counts, and current project path
- transport status now also surfaces the current timeline/transport backend seam and any live-playback disable reason when cached preview is being used
- project/session state now also preserves engine integration metadata for timeline backend, transport backend, and Tracktion sync/fallback status across save/reopen
- the top session summary now reflects timeline backend, transport backend, and current sync/live-fallback state directly in the main toolbar
- the top session summary now also includes the current sync/fallback reason, so preview-load failures and route changes are visible without opening logs
- runtime transport sync now flows back into persisted engine integration state, so Tracktion seam status is updated during play/pause/seek/rebuild/fallback transitions instead of only at project open/save time
- transport now clears stale selected-source state after destructive edits or vanished selection targets, so fallback transitions do not keep an invalid loaded source alive
- if live/cached preview preparation fails, the app now drops invalid preview route state and reports a clean no-source fallback instead of pretending an old preview source is still usable
- project playback now prefers a cached project preview mix as the stable main route, so clip gain/fade/mute/solo/crossfade changes are heard at the project level instead of relying on raw selected-source playback
- transport route changes between `project-live`, `project-cached-preview`, `selected-source`, and `no-source` are now logged explicitly, so fallback transitions are easier to diagnose during QA
- closing the app autosaves, saves the project, and warns before quitting if tasks are still running or unsaved edits remain
- startup can restore from `autosave.project.json` for the default workspace project

Проверка desktop-части:
1. Соберите приложение с подключенным JUCE.
2. Запустите `moon_audio_editor.exe` из `build`, либо через install/package flow.
3. Используйте `New`, `Open`, `Save`, `Import WAV`, `Export Mix`, `Export Region`, `Export Stems` или drag-and-drop `.wav` в окно.
4. Проверьте, что:
- клип появляется на `Track 1`
- ширина клипа соответствует длительности файла
- рисуется rough waveform
- работает выбор клипа и региона
- клип можно двигать по timeline, и позиция сохраняется после save/reopen
- работают gain, trim, split, fade in/out, duplicate, delete и take activation
- stems/rewrite/add-layer создают задачи и вставляют generated clips
- reopen проекта восстанавливает клипы, generated assets, fade-параметры, take state и waveform
- `Settings` сохраняет backend URL, cache directory и default sample rate
- `TaskPanel` показывает progress, логи и пишет `logs/app.log`
- `Export Mix`, `Export Region` и `Export Stems` рендерят слышимый WAV из активных клипов
- экспорт учитывает gain, fade in/out, clip offsets, границы региона, active takes и mute/solo дорожек
- при наличии `autosave.project.json` старт может восстановиться из autosave для default workspace project

### Useful shortcuts / Полезные шорткаты
- `Ctrl+N` new project
- `Ctrl+O` open project
- `Ctrl+S` save project
- `Ctrl+I` import WAV
- `Ctrl+D` duplicate selected clip
- `Ctrl+E` split selected clip at playhead
- `Ctrl+T` activate selected take
- `Delete` or `Backspace` delete selected clip
- `Space` play/pause
- `Left` / `Right` nudge playhead by `0.1s`
- `Shift+Left` / `Shift+Right` nudge playhead by `1.0s`
- `Home` seek to `0.0s`
- `Escape` clear selected region

### Release Notes / Почти ship-ready
- `backend\run_backend.bat` bootstraps the local Python backend on Windows
- `backend\run_backend.bat` echoes active Demucs / ACE-Step env settings before launch so runner wiring is visible at first run
- `backend\run_backend.bat` also echoes optional runner timeout settings so long-running local inference configuration is visible during handoff
- `package_release.bat` runs configure, build, install, and package steps for a Release handoff
- `package_release.bat` now also accepts `MOON_ENABLE_TRACKTION=ON` for a Tracktion-seam build handoff
- `package_release.bat` now also prints the build/install locations and points the operator to the release handoff docs after packaging
- `cpack` produces a ZIP package by default and also an NSIS installer on Windows when NSIS is installed

## Architecture Summary / Кратко об архитектуре
Desktop:
- bootstrap and app lifecycle
- project/session management
- timeline, transport, waveform, inspector, and task/log UI
- local backend polling and result insertion
- Windows-ready install/package flow

Backend:
- health/models endpoints
- in-memory job store
- cache key generation
- swappable AI service adapters
- optional subprocess-based Demucs / ACE-Step hooks with safe local fallback when executables are missing
- Demucs can be driven through a local command string, and ACE-Step can be driven through a local `infer-api.py` style HTTP runner

Desktop:
- bootstrap приложения и lifecycle окна
- управление проектом и сессией
- timeline, transport, waveform, inspector и task/log UI
- polling локального backend и вставка результатов
- install/package-путь для Windows-ready поставки

Backend:
- endpoints health/models
- in-memory job store
- генерация cache key
- заменяемые AI service adapters

## Current Feature Status / Текущий статус функций
- Project create/open/save: working JSON session flow with autosave recovery
- Audio import: WAV import through button and drag-and-drop
- Waveform display: rough waveform peaks from WAV analysis
- Transport: JUCE-backed playback path when JUCE is available, including cached whole-timeline preview playback
- Preview workflow: JUCE builds prefer live project playback for compatible WAV sessions, while cached preview remains the fallback path with explicit rebuild controls when needed
- Preview workflow: stale project preview is auto-maintained while idle, with `Rebuild Preview` available as an explicit manual refresh
- Transport model: timeline seek/scrub/play now defaults to project-wide preview transport instead of selected-clip-local playback
- Backend jobs: local FastAPI contract plus desktop HTTP/stub fallback
- Backend adapters: jobs now go through swappable service layers, with optional real Demucs/ACE-Step subprocess hooks and safe WAV fallback
- Backend adapters: Demucs uses a real local command path when configured, while ACE-Step prefers a local API runner and falls back safely when unavailable
- Editing: clip selection, region selection, move, duplicate/delete, gain, trim, split, fade controls, take activation, playback-safe interactive clip edits, inspector-visible automatic crossfade diagnostics, and adjustable `Crossfade Prev/Next` overlap editing
- Timeline polish: visible playhead, ruler scrubbing, keyboard playhead nudging, explicit region clearing, and same-track overlap/crossfade highlighting directly on the timeline
- Recovery polish: shutdown now autosaves and warns on quit while tasks are active
- Startup diagnostics: the app can surface autosave restore and backend fallback notices immediately after launch
- Recovery UX: startup/recovery notices remain visible in the main window until dismissed
- Recovery UX: backend refresh no longer wipes autosave recovery notice state
- Desktop UX: common project/import/export/AI action failures now surface immediate dialogs in addition to structured logs
- Desktop UX: the window title now reflects the active project and unsaved-change state
- Desktop UX: unavailable export/preview/AI actions are disabled until the required selection or project state exists
- Desktop UX: the top bar now exposes a live session summary for quick orientation inside the current project
- Backend UX: the desktop app can refresh backend health/model availability from the toolbar after launch
- Preview UX: the desktop app can explicitly rebuild the cached timeline preview from the toolbar after edits
- Task diagnostics: failed backend jobs are surfaced in a dedicated latest-failure banner and no longer linger as pending insertions
- Take polish: rewrite takes join a take group, active/alt state is visible, and export respects active takes
- Export/render: WAV export now mixes real PCM audio from audible active clips with gain, fades, offsets, region trimming, track mute/solo awareness, and automatic equal-power crossfades for same-track overlaps
- Packaging readiness: `install()` rules and CPack metadata are in place for ZIP/NSIS-style Windows distribution
- Track polish: `TrackListView` now exposes live mute/solo toggles and add-track action, which feed directly into export audibility
- Tracktion seam: timeline and transport facades now expose a hybrid Tracktion entry point with lightweight fallback, and the project model persists backend/sync metadata so the app has a real architectural switch instead of placeholder comments only
- Tracktion seam: runtime transport transitions now feed the same persisted integration metadata through a dedicated sync bridge, which makes future Tracktion-backed runtime hookup easier without breaking the lightweight path
- Tracktion seam: runtime coordination now has an explicit engine-level touchpoint instead of living only inside controller glue, which makes future Tracktion-backed transport/timeline hookup safer to extend

## Roadmap / Дорожная карта
- Phase 1: shell, layout, project/session foundation
- Phase 2: waveform import/playback and selection
- Phase 3: backend polling and job lifecycle
- Phase 4: stems, rewrite, add-layer end-to-end
- Phase 5: export, autosave, polish, and packaging readiness

## Known Limitations / Известные ограничения
- Real JUCE playback still depends on a local JUCE installation and native Windows toolchain.
- Tracktion Engine integration is now seam-first / hybrid, but it still routes core runtime behavior through the existing lightweight path until a fuller Tracktion-backed edit graph is wired in.
- AI model execution is stubbed behind swappable service adapters until the concrete local models are wired in.
- Set `MOON_DEMUCS_EXECUTABLE` to a local Demucs command such as `python -m demucs`, and set `MOON_ACE_STEP_API_URL` / `MOON_ACE_STEP_CHECKPOINT_PATH` when running a local ACE-Step `infer-api.py` service. If those runners are unavailable, the backend keeps using safe local fallback outputs.
- Playback now supports a live project mix source in JUCE builds for compatible WAV sessions, including automatic same-track overlap crossfades and safer controller-managed edit handoff during timeline changes, but it is still not a full DAW-grade engine with advanced routing, automation, FX, and editing during playback.
- Export currently targets PCM WAV sources and a simple internal mixdown path; advanced DAW-grade routing, automation, FX, time-stretching, and clip warping are still ahead.
- In this environment I could not complete a local C++ build because the compiler toolchain is not installed.

## Final QA Checklist / Финальный QA-чеклист
- Configure a Windows Release build with JUCE available; optionally enable `MOON_ENABLE_TRACKTION=ON`
- Launch `backend\run_backend.bat` and verify `/health` + `/models`
- Confirm the desktop status bar reports backend live/fallback plus timeline/transport backend seam
- Create a new project, import WAV, save, reopen, and confirm restored clips/assets/state
- Verify play/pause/seek, project-wide preview, and fallback behavior after edits
- Verify move/trim/split/fade/take/crossfade edits while transport is idle and while playback was active just before the edit
- Run stems, rewrite, and add-layer once with fallback runners and once with real local runners if configured
- Export mix / region / stems and confirm active takes, mute/solo, fades, and overlaps are respected
- Run `cmake --install` and `cpack`, then smoke-test the packaged handoff on a clean machine if available
- Use `docs/release_checklist.md` as the handoff-oriented Windows QA checklist when preparing a release candidate
- Use `docs/windows_handoff.md` as the build/package/run handoff guide for the actual Windows operator
