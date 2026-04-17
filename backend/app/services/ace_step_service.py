from __future__ import annotations

import json
import shutil
import subprocess
import urllib.error
import urllib.request
from pathlib import Path

from app.audio_utils import copy_or_create_wav_like
from app.config import settings


class AceStepService:
    def __init__(self, executable: str = "", api_url: str = "", checkpoint_path: str = "") -> None:
        self.executable = executable.strip()
        self.api_url = api_url.rstrip("/")
        self.checkpoint_path = checkpoint_path.strip()
        self._last_error = ""
        self._last_run_details: dict[str, object] = {}

    def is_available(self) -> bool:
        return self.api_available() or bool(self.executable) or shutil.which("ace_step") is not None

    def api_available(self) -> bool:
        if not self.api_url:
            return False

        try:
            with urllib.request.urlopen(f"{self.api_url}/health", timeout=settings.ace_step_health_timeout_sec) as response:
                return 200 <= response.status < 300
        except Exception:
            return False

    def runtime_summary(self) -> dict[str, object]:
        api_ready = self.api_available()
        executable_ready = bool(self.executable) or shutil.which("ace_step") is not None
        if api_ready:
            mode = "api"
        elif executable_ready:
            mode = "subprocess"
        else:
            mode = "fallback"

        return {
            "available": api_ready or executable_ready,
            "mode": mode,
            "api_url": self.api_url,
            "checkpoint_path": self.checkpoint_path,
            "command": self.executable or shutil.which("ace_step") or "",
            "health_timeout_sec": settings.ace_step_health_timeout_sec,
            "generate_timeout_sec": settings.ace_step_generate_timeout_sec,
            "last_error": self._last_error,
            "last_run": self._last_run_details,
        }

    def generate(self, input_audio_path: str, prompt: str, params: dict, output_path: str) -> str:
        if self.api_available():
            try:
                generated = self._generate_via_api(input_audio_path, prompt, params, output_path)
                self._last_error = ""
                self._last_run_details = {
                    "mode": "api",
                    "api_url": self.api_url,
                    "output_path": generated,
                }
                return generated
            except Exception as exc:
                self._last_error = f"api: {exc}"
                self._last_run_details = {
                    "mode": "fallback",
                    "reason": self._last_error,
                }

        if self.is_available():
            executable = self.executable or "ace_step"
            command = [
                executable,
                "--input",
                input_audio_path,
                "--output",
                output_path,
                "--prompt",
                prompt,
            ]
            reference_path = params.get("reference_audio_path")
            if reference_path:
                command.extend(["--reference", str(reference_path)])
            if params.get("seed") is not None:
                command.extend(["--seed", str(params["seed"])])
            if params.get("strength") is not None:
                command.extend(["--strength", str(params["strength"])])
            if params.get("duration_sec") is not None:
                command.extend(["--duration", str(params["duration_sec"])])

            try:
                completed = subprocess.run(
                    command,
                    check=True,
                    capture_output=True,
                    text=True,
                    timeout=settings.ace_step_generate_timeout_sec,
                )
                if Path(output_path).exists():
                    self._last_error = ""
                    self._last_run_details = {
                        "mode": "subprocess",
                        "command": command,
                        "output_path": output_path,
                        "stdout_tail": self._truncate_output(completed.stdout),
                        "stderr_tail": self._truncate_output(completed.stderr),
                    }
                    return str(output_path)
            except Exception as exc:
                self._last_error = f"subprocess: {self._format_subprocess_error(exc)}"
                self._last_run_details = {
                    "mode": "fallback",
                    "command": command,
                    "reason": self._last_error,
                }

        duration = max(0.1, float(params.get("duration_sec", 10.0) or 10.0))
        fallback_path = str(copy_or_create_wav_like(input_audio_path, output_path, duration_sec=duration))
        if not self._last_run_details:
            self._last_run_details = {
                "mode": "fallback",
                "reason": "runner unavailable",
                "output_path": fallback_path,
            }
        return fallback_path

    def _generate_via_api(self, input_audio_path: str, prompt: str, params: dict, output_path: str) -> str:
        payload = {
            "checkpoint_path": self.checkpoint_path,
            "bf16": True,
            "torch_compile": False,
            "device_id": 0,
            "output_path": output_path,
            "audio_duration": max(0.1, float(params.get("duration_sec", 10.0) or 10.0)),
            "prompt": prompt,
            "lyrics": params.get("lyrics", ""),
            "infer_step": int(params.get("infer_step", 27) or 27),
            "guidance_scale": float(params.get("strength", 5.0) or 5.0),
            "scheduler_type": params.get("scheduler_type", "euler"),
            "cfg_type": params.get("cfg_type", "apg"),
            "omega_scale": float(params.get("omega_scale", 10.0) or 10.0),
            "actual_seeds": [int(params.get("seed", 0) or 0)],
            "guidance_interval": float(params.get("guidance_interval", 0.5) or 0.5),
            "guidance_interval_decay": float(params.get("guidance_interval_decay", 0.0) or 0.0),
            "min_guidance_scale": float(params.get("min_guidance_scale", 1.0) or 1.0),
            "use_erg_tag": bool(params.get("use_erg_tag", True)),
            "use_erg_lyric": bool(params.get("use_erg_lyric", False)),
            "use_erg_diffusion": bool(params.get("use_erg_diffusion", False)),
            "oss_steps": params.get("oss_steps", [12, 24]),
            "guidance_scale_text": float(params.get("guidance_scale_text", 0.0) or 0.0),
            "guidance_scale_lyric": float(params.get("guidance_scale_lyric", 0.0) or 0.0),
        }

        request = urllib.request.Request(
            f"{self.api_url}/generate",
            data=json.dumps(payload).encode("utf-8"),
            headers={"Content-Type": "application/json"},
            method="POST",
        )
        with urllib.request.urlopen(request, timeout=settings.ace_step_generate_timeout_sec) as response:
            result = json.loads(response.read().decode("utf-8"))
        generated_path = (
            result.get("output_audio_path")
            or result.get("output_path")
            or result.get("audio_path")
            or output_path
        )
        if not Path(generated_path).exists():
            raise FileNotFoundError(f"ACE-Step API returned missing output path: {generated_path}")
        return str(Path(generated_path))

    @staticmethod
    def _truncate_output(value: str, limit: int = 400) -> str:
        text = (value or "").strip()
        if len(text) <= limit:
            return text
        return text[-limit:]

    def _format_subprocess_error(self, exc: Exception) -> str:
        if isinstance(exc, subprocess.TimeoutExpired):
            return f"timeout after {settings.ace_step_generate_timeout_sec}s"
        if isinstance(exc, subprocess.CalledProcessError):
            stderr = self._truncate_output(exc.stderr or "")
            stdout = self._truncate_output(exc.stdout or "")
            detail = stderr or stdout or str(exc)
            return f"subprocess exit {exc.returncode}: {detail}"
        return str(exc)
