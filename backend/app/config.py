import os
from pathlib import Path


class Settings:
    backend_name = "local-ai-audio-service"
    cache_dir = Path("cache")
    demucs_executable = os.getenv("MOON_DEMUCS_EXECUTABLE", "").strip()
    demucs_timeout_sec = int(os.getenv("MOON_DEMUCS_TIMEOUT_SEC", "1800"))
    ace_step_executable = os.getenv("MOON_ACE_STEP_EXECUTABLE", "").strip()
    ace_step_api_url = os.getenv("MOON_ACE_STEP_API_URL", "http://127.0.0.1:8001").strip()
    ace_step_checkpoint_path = os.getenv("MOON_ACE_STEP_CHECKPOINT_PATH", "").strip()
    ace_step_health_timeout_sec = int(os.getenv("MOON_ACE_STEP_HEALTH_TIMEOUT_SEC", "3"))
    ace_step_generate_timeout_sec = int(os.getenv("MOON_ACE_STEP_GENERATE_TIMEOUT_SEC", "600"))
    ffmpeg_executable = os.getenv("MOON_FFMPEG_EXECUTABLE", "").strip()


settings = Settings()
