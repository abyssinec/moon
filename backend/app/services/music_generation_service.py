from __future__ import annotations

from app.config import settings
from app.services.ace_step_service import AceStepService


class MusicGenerationService:
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

    def generate_music(
        self,
        prompt: str,
        params: dict,
        output_path: str,
        progress_callback=None,
        cancel_check=None,
    ) -> str:
        return self.ace_step_service.generate(
            None,
            prompt,
            params,
            output_path,
            progress_callback=progress_callback,
            cancel_check=cancel_check,
            allow_fallback=False,
        )
