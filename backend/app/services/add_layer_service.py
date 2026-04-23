from __future__ import annotations

from app.config import settings
from app.services.ace_step_service import AceStepService


class AddLayerService:
    def __init__(self, ace_step_service: AceStepService | None = None) -> None:
        self.ace_step_service = ace_step_service or AceStepService(
            settings.ace_step_executable,
            settings.ace_step_api_url,
            settings.ace_step_checkpoint_path,
        )

    def available_models(self) -> list[str]:
        return ["ace_step"] if self.ace_step_service.is_available() else []

    def runtime_summary(self) -> dict[str, object]:
        return self.ace_step_service.runtime_summary()

    def run_add_layer(self, input_audio_path: str, prompt: str, params: dict, output_path: str) -> str:
        return self.ace_step_service.generate(
            input_audio_path,
            prompt,
            params,
            output_path,
            allow_fallback=False,
        )
