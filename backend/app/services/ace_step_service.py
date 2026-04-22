from __future__ import annotations

import importlib.util
import json
import os
import shutil
import subprocess
import sys
import time
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
        return self.api_available() or self._resolve_launch_command() is not None

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
        resolved_command = self._resolve_launch_command()
        executable_ready = resolved_command is not None
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
            "command": " ".join(resolved_command) if resolved_command else "",
            "health_timeout_sec": settings.ace_step_health_timeout_sec,
            "generate_timeout_sec": settings.ace_step_generate_timeout_sec,
            "last_error": self._last_error,
            "last_run": self._last_run_details,
        }

    def generate(
        self,
        input_audio_path: str | None,
        prompt: str,
        params: dict,
        output_path: str,
        progress_callback=None,
        cancel_check=None,
        allow_fallback: bool = True,
    ) -> str:
        def emit(stage: str, progress: float, message: str) -> None:
            if progress_callback is not None:
                progress_callback(stage, progress, message)

        def cancelled() -> bool:
            return bool(cancel_check and cancel_check())

        emit("preparing", 0.10, "Preparing runtime")
        if cancelled():
            raise RuntimeError("cancelled")

        if self.api_available():
            try:
                emit("starting_runtime", 0.20, "Connecting to ACE-Step API")
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
            command = list(self._resolve_launch_command() or ["ace_step"])
            command.extend(["--output", output_path, "--prompt", prompt])
            if input_audio_path:
                command.extend(["--input", input_audio_path])
            reference_path = params.get("reference_audio_path")
            if reference_path:
                command.extend(["--reference", str(reference_path)])
            checkpoint_path = str(params.get("checkpoint_path") or self.checkpoint_path or "").strip()
            if checkpoint_path:
                command.extend(["--checkpoint", checkpoint_path])
            if params.get("seed") is not None:
                command.extend(["--seed", str(params["seed"])])
            if params.get("strength") is not None:
                command.extend(["--strength", str(params["strength"])])
            if params.get("duration_sec") is not None:
                command.extend(["--duration", str(params["duration_sec"])])
            if params.get("device"):
                command.extend(["--device", str(params["device"])])
            if params.get("lyrics"):
                command.extend(["--lyrics", str(params["lyrics"])])
            if params.get("notes"):
                command.extend(["--notes", str(params["notes"])])

            try:
                emit("loading_model", 0.28, "Loading model")
                process = subprocess.Popen(
                    command,
                    capture_output=True,
                    text=True,
                )
                start_time = time.monotonic()
                last_progress = 0.35
                while True:
                    if cancelled():
                        process.terminate()
                        try:
                            process.wait(timeout=5)
                        except subprocess.TimeoutExpired:
                            process.kill()
                        raise RuntimeError("cancelled")

                    return_code = process.poll()
                    if return_code is not None:
                        stdout, stderr = process.communicate()
                        if return_code == 0 and Path(output_path).exists():
                            self._last_error = ""
                            self._last_run_details = {
                                "mode": "subprocess",
                                "command": command,
                                "output_path": output_path,
                                "stdout_tail": self._truncate_output(stdout),
                                "stderr_tail": self._truncate_output(stderr),
                            }
                            emit("finalizing", 0.92, "Finalizing output")
                            return str(output_path)

                        detail = self._truncate_output(stderr or stdout or "")
                        raise subprocess.CalledProcessError(return_code, command, output=stdout, stderr=detail)

                    elapsed = time.monotonic() - start_time
                    if elapsed > settings.ace_step_generate_timeout_sec:
                        process.terminate()
                        try:
                            process.wait(timeout=5)
                        except subprocess.TimeoutExpired:
                            process.kill()
                        raise subprocess.TimeoutExpired(command, settings.ace_step_generate_timeout_sec)

                    target_progress = min(0.88, 0.35 + (elapsed / max(30.0, settings.ace_step_generate_timeout_sec)) * 0.50)
                    if target_progress > last_progress + 0.01:
                        last_progress = target_progress
                        emit("generating", last_progress, "Generating audio")
                    time.sleep(0.25)

                if Path(output_path).exists():
                    self._last_error = ""
                    self._last_run_details = {
                        "mode": "subprocess",
                        "command": command,
                        "output_path": output_path,
                        "stdout_tail": "",
                        "stderr_tail": "",
                    }
                    return str(output_path)
            except Exception as exc:
                self._last_error = f"subprocess: {self._format_subprocess_error(exc)}"
                self._last_run_details = {
                    "mode": "fallback",
                    "command": command,
                    "reason": self._last_error,
                }

        if not allow_fallback:
            raise RuntimeError(self._last_error or "ACE-Step runtime is unavailable")

        duration = max(0.1, float(params.get("duration_sec", 10.0) or 10.0))
        fallback_path = str(copy_or_create_wav_like(input_audio_path, output_path, duration_sec=duration))
        if not self._last_run_details:
            self._last_run_details = {
                "mode": "fallback",
                "reason": "runner unavailable",
                "output_path": fallback_path,
            }
        return fallback_path

    def _module_candidates(self) -> list[str]:
        values: list[str] = []
        configured = os.getenv("MOON_ACE_STEP_RUNTIME_MODULE", "").strip()
        if configured:
            values.append(configured)
        for candidate in [
            "ace_step",
            "acestep",
            "ace_step.cli",
            "acestep.cli",
            "ace_step_api",
            "acestep_api",
        ]:
            if candidate not in values:
                values.append(candidate)
        return values

    def _resolve_launch_command(self) -> list[str] | None:
        if self.executable and Path(self.executable).exists():
            return [self.executable]

        env_executable = os.getenv("MOON_ACE_STEP_EXECUTABLE", "").strip()
        if env_executable and Path(env_executable).exists():
            return [env_executable]

        backend_root = Path(__file__).resolve().parents[2]
        scripts_dir = backend_root / ".venv" / "Scripts"
        for candidate in [
            scripts_dir / "ace_step.exe",
            scripts_dir / "ace_step.cmd",
            scripts_dir / "ace_step.bat",
            scripts_dir / "ace_step",
            scripts_dir / "acestep.exe",
            scripts_dir / "acestep.cmd",
            scripts_dir / "acestep.bat",
            scripts_dir / "acestep",
            scripts_dir / "acestep-api.exe",
            scripts_dir / "acestep-api.cmd",
            scripts_dir / "acestep_api.exe",
            scripts_dir / "ace_step_api.exe",
        ]:
            if candidate.exists():
                return [str(candidate)]

        for candidate in [
            scripts_dir / "ace_step-script.py",
            scripts_dir / "acestep-script.py",
            scripts_dir / "ace_step_api-script.py",
            scripts_dir / "acestep_api-script.py",
        ]:
            if candidate.exists():
                return [sys.executable, str(candidate)]

        for module_name in self._module_candidates():
            try:
                if importlib.util.find_spec(module_name) is not None:
                    return [sys.executable, "-m", module_name]
            except ModuleNotFoundError:
                continue

        discovered = shutil.which("ace_step")
        if discovered:
            return [discovered]
        discovered_alt = shutil.which("acestep")
        return [discovered_alt] if discovered_alt else None

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
