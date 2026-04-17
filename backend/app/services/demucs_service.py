from __future__ import annotations

import shlex
import shutil
import subprocess
from pathlib import Path

from app.audio_utils import copy_or_create_wav_like
from app.config import settings


class DemucsService:
    def __init__(self, executable: str = "") -> None:
        self.executable = executable.strip()
        self._last_error = ""
        self._last_run_details: dict[str, object] = {}

    def is_available(self) -> bool:
        return bool(self.executable) or shutil.which("demucs") is not None

    def runtime_summary(self) -> dict[str, object]:
        resolved_command = self.executable or shutil.which("demucs") or ""
        return {
            "available": self.is_available(),
            "mode": "subprocess" if self.is_available() else "fallback",
            "command": resolved_command,
            "timeout_sec": settings.demucs_timeout_sec,
            "last_error": self._last_error,
            "last_run": self._last_run_details,
        }

    def run(self, input_audio_path: str, model_name: str, output_paths: dict[str, str]) -> dict[str, str]:
        if self.is_available():
            executable = self.executable or "demucs"
            output_root = Path(next(iter(output_paths.values()))).parent / "_demucs_temp"
            output_root.mkdir(parents=True, exist_ok=True)
            command = shlex.split(executable) + ["-n", model_name, "-o", str(output_root), input_audio_path]
            try:
                completed = subprocess.run(
                    command,
                    check=True,
                    capture_output=True,
                    text=True,
                    timeout=settings.demucs_timeout_sec,
                )
                model_dir = output_root / model_name / Path(input_audio_path).stem
                resolved: dict[str, str] = {}
                mapped_from: dict[str, str] = {}
                for stem_name, destination in output_paths.items():
                    candidates = self._candidate_paths(model_dir, stem_name)
                    source = next((candidate for candidate in candidates if candidate.exists()), None)
                    copy_or_create_wav_like(source, destination)
                    resolved[stem_name] = str(destination)
                    mapped_from[stem_name] = str(source) if source is not None else "fallback-copy"
                self._last_error = ""
                self._last_run_details = {
                    "mode": "subprocess",
                    "command": command,
                    "model_dir": str(model_dir),
                    "mapped_from": mapped_from,
                    "stdout_tail": self._truncate_output(completed.stdout),
                    "stderr_tail": self._truncate_output(completed.stderr),
                }
                return resolved
            except Exception as exc:
                self._last_error = self._format_subprocess_error(exc)
                self._last_run_details = {
                    "mode": "fallback",
                    "command": command,
                    "reason": self._last_error,
                }
                return self._fallback_outputs(input_audio_path, output_paths)

        return self._fallback_outputs(input_audio_path, output_paths)

    def _fallback_outputs(self, input_audio_path: str, output_paths: dict[str, str]) -> dict[str, str]:
        for destination in output_paths.values():
            copy_or_create_wav_like(input_audio_path, destination)
        if not self._last_run_details:
            self._last_run_details = {
                "mode": "fallback",
                "reason": "runner unavailable",
            }
        return {stem_name: str(destination) for stem_name, destination in output_paths.items()}

    @staticmethod
    def _candidate_paths(model_dir: Path, stem_name: str) -> list[Path]:
        aliases = {
            "vocals": ("vocals", "voice"),
            "drums": ("drums", "drum"),
            "bass": ("bass",),
            "other": ("other", "accompaniment", "no_vocals"),
        }
        names = aliases.get(stem_name.lower(), (stem_name.lower(),))
        candidates: list[Path] = []
        for name in names:
            candidates.append(model_dir / f"{name}.wav")
            candidates.append(model_dir / f"{name.upper()}.wav")
        return candidates

    @staticmethod
    def _truncate_output(value: str, limit: int = 400) -> str:
        text = (value or "").strip()
        if len(text) <= limit:
            return text
        return text[-limit:]

    def _format_subprocess_error(self, exc: Exception) -> str:
        if isinstance(exc, subprocess.TimeoutExpired):
            return f"timeout after {settings.demucs_timeout_sec}s"
        if isinstance(exc, subprocess.CalledProcessError):
            stderr = self._truncate_output(exc.stderr or "")
            stdout = self._truncate_output(exc.stdout or "")
            detail = stderr or stdout or str(exc)
            return f"subprocess exit {exc.returncode}: {detail}"
        return str(exc)
