from __future__ import annotations

from app.config import settings
from app.services.demucs_service import DemucsService


class StemsService:
    def __init__(self, demucs_service: DemucsService | None = None) -> None:
        self.demucs_service = demucs_service or DemucsService(settings.demucs_executable)

    def available_models(self) -> list[str]:
        models = ["demucs"]
        if not self.demucs_service.is_available():
            models.append("demucs_stub")
        return models

    def runtime_summary(self) -> dict[str, object]:
        return self.demucs_service.runtime_summary()

    def run_stems(self, input_audio_path: str, model_name: str, output_paths: dict[str, str]) -> dict[str, str]:
        return self.demucs_service.run(input_audio_path, "htdemucs" if model_name == "demucs" else model_name, output_paths)
