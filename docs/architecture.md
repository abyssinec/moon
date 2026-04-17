# Architecture / Архитектура

## English
The repository is split into three desktop-focused layers:

- `app`: application bootstrap, window lifecycle, top-level composition.
- `engine`: domain logic and application services such as project management, serialization, task tracking, transport abstractions, waveform services, export, logging, settings, an explicit runtime coordination touchpoint, and the hybrid Tracktion seam used to prepare future edit-graph synchronization without breaking the lightweight runtime path.
- `ui`: visual components for transport, timeline, tracks, inspector, task view, waveform clips, and region selection.

The local backend is a separate FastAPI process on `localhost`. It owns job execution, service adapters, cache-key generation, and deterministic artifact storage. The desktop app never modifies source files in place and only references generated outputs from cache or project-managed generated directories.

## Русский
Репозиторий разделен на три desktop-слоя:

- `app`: bootstrap приложения, lifecycle окна, верхнеуровневая композиция.
- `engine`: доменная логика и сервисы приложения, включая управление проектом, сериализацию, task tracking, transport abstractions, waveform services, export, logging и settings.
- `ui`: визуальные компоненты transport, timeline, списка треков, inspector, task view, waveform clips и region selection.

Локальный backend запускается отдельным FastAPI-процессом на `localhost`. Он отвечает за выполнение job, service adapters, генерацию cache key и детерминированное хранение артефактов. Desktop-приложение никогда не меняет исходные файлы на месте и работает только со ссылками на сгенерированные артефакты из cache или управляемых директорий проекта.
