import os
from pathlib import Path


def _first_existing_dir(candidates: list[Path]) -> Path:
    for candidate in candidates:
        if candidate.exists():
            return candidate
    return candidates[0]


class Settings:
    backend_name = "local-ai-audio-service"

    backend_root = Path(__file__).resolve().parents[1]
    data_root = backend_root.parent if backend_root.name == "backend" else backend_root
    cache_dir = backend_root / "cache"

    demucs_executable = os.getenv("MOON_DEMUCS_EXECUTABLE", "").strip()
    demucs_timeout_sec = int(os.getenv("MOON_DEMUCS_TIMEOUT_SEC", "1800"))

    ace_step_executable = os.getenv("MOON_ACE_STEP_EXECUTABLE", "").strip()
    ace_step_api_url = ""
    ffmpeg_executable = os.getenv("MOON_FFMPEG_EXECUTABLE", "").strip()

    _configured_checkpoint = os.getenv("MOON_ACE_STEP_CHECKPOINT_PATH", "").strip()
    _packaged_checkpoint = data_root / "models" / "ace-step"
    _backend_local_checkpoint = backend_root / "models" / "ace-step"
    _user_cache_checkpoint = Path.home() / ".cache" / "ace-step" / "checkpoints"

    ace_step_checkpoint_path = str(
        _first_existing_dir([
            _packaged_checkpoint,
            Path(_configured_checkpoint) if _configured_checkpoint else _packaged_checkpoint,
            _backend_local_checkpoint,
            _user_cache_checkpoint,
        ])
    )

    ace_step_health_timeout_sec = int(os.getenv("MOON_ACE_STEP_HEALTH_TIMEOUT_SEC", "3"))
    ace_step_generate_timeout_sec = int(os.getenv("MOON_ACE_STEP_GENERATE_TIMEOUT_SEC", "1800"))
    python_backend_debug_log_path = str(
        Path(os.getenv("LOCALAPPDATA", str(Path.cwd()))).resolve() / "MoonAudioEditor" / "logs" / "python_backend_debug.log"
    )


settings = Settings()
