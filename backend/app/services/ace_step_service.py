from __future__ import annotations

import inspect
import os
import threading
import traceback
from datetime import datetime, timezone
from pathlib import Path

from app.config import settings

try:
    import numpy as np
except Exception:
    np = None

try:
    import soundfile as sf
except Exception:
    sf = None

try:
    import torch
except Exception:
    torch = None

try:
    import torchaudio
except Exception:
    torchaudio = None

try:
    import acestep.pipeline_ace_step as _acestep_pipeline_module
    from acestep.pipeline_ace_step import ACEStepPipeline
    _ACESTEP_IMPORT_ERROR = ""
except Exception as exc:
    _acestep_pipeline_module = None
    ACEStepPipeline = None
    _ACESTEP_IMPORT_ERROR = str(exc)


_PIPELINE = None
_PIPELINE_KEY = None
_PIPELINE_LOCK = threading.RLock()
_GENERATE_LOCK = threading.Lock()
_REQUIRED_CHECKPOINT_DIRS = {
    "music_dcae_f8c8",
    "music_vocoder",
    "ace_step_transformer",
    "umt5-base",
}


def _debug_log_path() -> Path:
    configured = getattr(settings, "python_backend_debug_log_path", "").strip()
    if configured:
        return Path(configured)
    local_app_data = os.getenv("LOCALAPPDATA", "").strip()
    if local_app_data:
        return Path(local_app_data) / "MoonAudioEditor" / "logs" / "python_backend_debug.log"
    return Path.cwd() / "python_backend_debug.log"


def _write_debug_log(message: str) -> None:
    try:
        path = _debug_log_path()
        path.parent.mkdir(parents=True, exist_ok=True)
        with path.open("a", encoding="utf-8") as handle:
            handle.write(message.rstrip())
            handle.write("\n")
    except Exception:
        pass


def _debug_event(event: str, **details) -> None:
    timestamp = datetime.now(timezone.utc).isoformat()
    lines = [f"[{timestamp}] {event}"]
    for key, value in details.items():
        lines.append(f"  {key}: {value}")
    _write_debug_log("\n".join(lines))


def _cuda_available() -> bool:
    try:
        return bool(torch is not None and torch.cuda.is_available())
    except Exception:
        return False


def _torch_version() -> str:
    try:
        return str(torch.__version__) if torch is not None else "unavailable"
    except Exception:
        return "unavailable"


def _normalize_audio_array(src, channels_first=True):
    if src is None:
        raise RuntimeError("audio tensor is empty")
    if torch is None:
        raise RuntimeError("torch is required to normalize generated audio")

    tensor = src.detach().cpu()
    while tensor.ndim > 2 and 1 in tensor.shape:
        tensor = tensor.squeeze()

    if tensor.ndim == 0:
        raise RuntimeError("audio tensor is scalar")

    if tensor.ndim == 1:
        data = tensor.numpy()
    elif tensor.ndim == 2:
        data = tensor.transpose(0, 1).contiguous().numpy() if channels_first else tensor.contiguous().numpy()
    else:
        flat = tensor.reshape(tensor.shape[0], -1)
        data = flat.transpose(0, 1).contiguous().numpy() if channels_first else flat.contiguous().numpy()

    if np is None:
        raise RuntimeError("numpy is required to save generated audio")

    data = np.asarray(data, dtype=np.float32)
    if data.ndim == 0:
        data = data.reshape(1, 1)
    elif data.ndim == 1:
        data = data.reshape(-1, 1)
    elif data.ndim > 2:
        data = data.reshape(data.shape[0], -1)

    data = np.nan_to_num(data, nan=0.0, posinf=0.0, neginf=0.0)
    data = np.clip(data, -1.0, 1.0)
    return np.ascontiguousarray(data)


def _write_pcm16_wav(output_path: Path, data, sample_rate: int) -> None:
    import wave

    pcm = (data * 32767.0).astype(np.int16, copy=False)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(output_path), "wb") as handle:
        handle.setnchannels(int(pcm.shape[1]))
        handle.setsampwidth(2)
        handle.setframerate(int(sample_rate))
        handle.writeframes(pcm.tobytes())


def _patch_torchaudio_save() -> None:
    if torchaudio is None:
        return

    def _moon_torchaudio_save(
        uri,
        src,
        sample_rate,
        channels_first=True,
        format=None,
        encoding=None,
        bits_per_sample=None,
        backend=None,
        compression=None,
    ):
        del format, encoding, bits_per_sample, backend, compression
        output_path = Path(uri)
        data = _normalize_audio_array(src, channels_first=channels_first)
        output_path.parent.mkdir(parents=True, exist_ok=True)

        sf_error = None
        if sf is not None:
            try:
                sf.write(str(output_path), data, int(sample_rate), subtype="PCM_16")
                return
            except Exception as exc:
                sf_error = exc

        try:
            _write_pcm16_wav(output_path, data, int(sample_rate))
            return
        except Exception as exc:
            raise RuntimeError(
                f"audio save failed path={output_path} sample_rate={sample_rate} "
                f"shape={getattr(data, 'shape', None)} dtype={getattr(data, 'dtype', None)} "
                f"soundfile={sf_error} fallback={exc}"
            ) from exc

    torchaudio.save = _moon_torchaudio_save


_patch_torchaudio_save()


def _patch_acestep_tqdm() -> None:
    if _acestep_pipeline_module is None:
        return

    original = getattr(_acestep_pipeline_module, "tqdm", None)
    if original is None or getattr(original, "__name__", "") == "_moon_safe_tqdm":
        return

    def _moon_safe_tqdm(*args, **kwargs):
        kwargs["disable"] = True
        try:
            return original(*args, **kwargs)
        except OSError:
            if args:
                return args[0]
            return []

    setattr(_moon_safe_tqdm, "__name__", "_moon_safe_tqdm")
    _acestep_pipeline_module.tqdm = _moon_safe_tqdm


_patch_acestep_tqdm()


class AceStepService:
    def __init__(self, executable: str = "", api_url: str = "", checkpoint_path: str = "") -> None:
        self.executable = executable.strip()
        self.api_url = api_url.strip()
        self.checkpoint_path = checkpoint_path.strip()
        self._last_error = ""
        self._last_run_details: dict[str, object] = {}

    def _resolved_checkpoint_path(self, params: dict | None = None) -> str:
        params = params or {}
        requested = str(
            params.get("checkpoint_path")
            or self.checkpoint_path
            or settings.ace_step_checkpoint_path
            or ""
        ).strip()
        resolved = self._find_runtime_checkpoint_dir(requested)
        return str(resolved if resolved is not None else requested)

    def _find_runtime_checkpoint_dir(self, checkpoint_path: str) -> Path | None:
        if not checkpoint_path:
            return None

        root = Path(checkpoint_path)
        if not root.exists():
            return None

        def has_required_dirs(candidate: Path) -> bool:
            return all((candidate / name).exists() for name in _REQUIRED_CHECKPOINT_DIRS)

        if has_required_dirs(root):
            return root

        direct_children = [entry for entry in root.iterdir() if entry.is_dir()]
        for child in direct_children:
            if has_required_dirs(child):
                return child

        for snapshot_root in root.glob("models--*\\snapshots\\*"):
            if snapshot_root.is_dir() and has_required_dirs(snapshot_root):
                return snapshot_root

        for nested in direct_children:
            snapshots_dir = nested / "snapshots"
            if not snapshots_dir.is_dir():
                continue
            for snapshot in snapshots_dir.iterdir():
                if snapshot.is_dir() and has_required_dirs(snapshot):
                    return snapshot

        return None

    def _checkpoint_exists(self, checkpoint_path: str) -> bool:
        return self._find_runtime_checkpoint_dir(checkpoint_path) is not None

    def embedded_available(self) -> bool:
        checkpoint = self._resolved_checkpoint_path()
        return ACEStepPipeline is not None and self._checkpoint_exists(checkpoint)

    def is_available(self) -> bool:
        return self.embedded_available()

    def api_available(self) -> bool:
        return False

    def runtime_summary(self) -> dict[str, object]:
        checkpoint = self._resolved_checkpoint_path()
        available = self.embedded_available()
        mode = "embedded" if available else "unavailable"
        return {
            "available": available,
            "mode": mode,
            "api_url": "",
            "checkpoint_path": checkpoint,
            "command": "",
            "health_timeout_sec": settings.ace_step_health_timeout_sec,
            "generate_timeout_sec": settings.ace_step_generate_timeout_sec,
            "supported_devices": ["auto", "gpu", "cpu"],
            "cuda_available": _cuda_available(),
            "torch_version": _torch_version(),
            "last_error": self._last_error or _ACESTEP_IMPORT_ERROR,
            "last_run": self._last_run_details,
            "debug_log_path": str(_debug_log_path()),
        }

    def _resolve_device(self, preference: str | None) -> tuple[str, str]:
        requested = str(preference or "auto").strip().lower()
        if requested in {"", "auto"}:
            return "auto", ("gpu" if _cuda_available() else "cpu")
        if requested in {"gpu", "cuda"}:
            if not _cuda_available():
                raise RuntimeError("GPU mode was requested, but CUDA is unavailable")
            return "gpu", "gpu"
        if requested == "cpu":
            if _cuda_available():
                raise RuntimeError("CPU mode is not supported by the current embedded ACE-Step build on this CUDA machine")
            return "cpu", "cpu"
        return requested, ("gpu" if _cuda_available() else "cpu")

    def _ensure_pipeline(self, params: dict):
        if ACEStepPipeline is None:
            raise RuntimeError(f"ACEStepPipeline import failed: {_ACESTEP_IMPORT_ERROR or 'unknown error'}")

        checkpoint_path = self._resolved_checkpoint_path(params)
        if not self._checkpoint_exists(checkpoint_path):
            raise RuntimeError(f"ACE-Step checkpoint path does not exist: {checkpoint_path}")

        requested_device, effective_device = self._resolve_device(params.get("device"))
        device_id = int(params.get("device_id", 0) or 0)
        dtype = "bfloat16" if effective_device == "gpu" else "float32"
        cpu_offload = effective_device == "cpu"
        torch_compile = bool(params.get("torch_compile", False) and effective_device == "gpu")
        overlapped_decode = bool(params.get("overlapped_decode", False) and effective_device == "gpu")

        key = (
            checkpoint_path,
            requested_device,
            effective_device,
            device_id,
            dtype,
            cpu_offload,
            torch_compile,
            overlapped_decode,
        )

        global _PIPELINE, _PIPELINE_KEY
        with _PIPELINE_LOCK:
            if _PIPELINE is not None and _PIPELINE_KEY == key:
                return _PIPELINE, requested_device, effective_device, device_id

            init_kwargs = {
                "checkpoint_dir": checkpoint_path,
                "device_id": device_id,
                "dtype": dtype,
                "cpu_offload": cpu_offload,
                "torch_compile": torch_compile,
                "overlapped_decode": overlapped_decode,
            }

            _debug_event(
                "pipeline.init",
                checkpoint_path=checkpoint_path,
                requested_device=requested_device,
                effective_device=effective_device,
                device_id=device_id,
                torch_version=_torch_version(),
                cuda_available=_cuda_available(),
                init_kwargs=init_kwargs,
            )

            _PIPELINE = ACEStepPipeline(**init_kwargs)
            _PIPELINE_KEY = key
            return _PIPELINE, requested_device, effective_device, device_id

    def _build_call_kwargs(
        self,
        pipeline,
        input_audio_path: str | None,
        prompt: str,
        params: dict,
        output_path: str,
    ) -> dict:
        call_sig = inspect.signature(pipeline.__call__)
        duration = max(0.1, float(params.get("duration_sec", 10.0) or 10.0))
        lyrics = str(params.get("lyrics", "") or "")
        seed = int(params.get("seed", 0) or 0)
        reference_audio_path = str(params.get("reference_audio_path", "") or "").strip()

        candidates = {
            "format": "wav",
            "audio_duration": duration,
            "prompt": prompt,
            "lyrics": lyrics,
            "infer_step": int(params.get("infer_step", 27) or 27),
            "guidance_scale": float(params.get("strength", 7.0) or 7.0),
            "scheduler_type": str(params.get("scheduler_type", "euler") or "euler"),
            "cfg_type": str(params.get("cfg_type", "apg") or "apg"),
            "omega_scale": float(params.get("omega_scale", 10.0) or 10.0),
            "manual_seeds": [seed],
            "guidance_interval": float(params.get("guidance_interval", 0.5) or 0.5),
            "guidance_interval_decay": float(params.get("guidance_interval_decay", 0.0) or 0.0),
            "min_guidance_scale": float(params.get("min_guidance_scale", 3.0) or 3.0),
            "use_erg_tag": bool(params.get("use_erg_tag", True)),
            "use_erg_lyric": bool(params.get("use_erg_lyric", bool(lyrics))),
            "use_erg_diffusion": bool(params.get("use_erg_diffusion", True)),
            "guidance_scale_text": float(params.get("guidance_scale_text", 0.0) or 0.0),
            "guidance_scale_lyric": float(params.get("guidance_scale_lyric", 0.0) or 0.0),
            "save_path": output_path,
            "batch_size": 1,
            "debug": True,
        }

        if input_audio_path:
            candidates["src_audio_path"] = input_audio_path
        if reference_audio_path:
            candidates["audio2audio_enable"] = True
            candidates["ref_audio_input"] = reference_audio_path
            candidates["ref_audio_strength"] = float(params.get("reference_strength", 0.5) or 0.5)

        call_kwargs = {}
        for key, value in candidates.items():
            if key in call_sig.parameters and value is not None:
                call_kwargs[key] = value
        return call_kwargs

    def _resolve_output_file(self, pipeline_result, requested_output_path: str) -> Path:
        requested = Path(requested_output_path)
        if requested.exists():
            return requested

        if isinstance(pipeline_result, (list, tuple)):
            for item in pipeline_result:
                if isinstance(item, str):
                    candidate = Path(item)
                    if candidate.suffix.lower() == ".wav" and candidate.exists():
                        return candidate

        raise RuntimeError(f"Generation finished but output file was not created: {requested_output_path}")

    def generate(
        self,
        input_audio_path: str | None,
        prompt: str,
        params: dict,
        output_path: str,
        progress_callback=None,
        cancel_check=None,
        allow_fallback: bool = False,
    ) -> str:
        del allow_fallback

        def emit(stage: str, progress: float, message: str) -> None:
            if progress_callback is not None:
                progress_callback(stage, progress, message)

        def cancelled() -> bool:
            return bool(cancel_check and cancel_check())

        checkpoint_path = self._resolved_checkpoint_path(params)
        _debug_event(
            "generation.enter",
            prompt=prompt,
            input_audio_path=input_audio_path,
            checkpoint_path=checkpoint_path,
            output_path=output_path,
            requested_device=params.get("device", "auto"),
        )
        emit("preparing", 0.08, "Preparing embedded ACE-Step runtime")
        if cancelled():
            raise RuntimeError("cancelled")
        if ACEStepPipeline is None:
            raise RuntimeError(f"Embedded ACE-Step runtime import failed: {_ACESTEP_IMPORT_ERROR or 'unknown error'}")
        if not self._checkpoint_exists(checkpoint_path):
            raise RuntimeError(f"Embedded ACE-Step checkpoint was not found: {checkpoint_path}")

        requested_output = Path(output_path)
        requested_output.parent.mkdir(parents=True, exist_ok=True)

        try:
            emit("loading_model", 0.20, "Loading embedded ACE-Step model")
            with _GENERATE_LOCK:
                pipeline, requested_device, effective_device, device_id = self._ensure_pipeline(params)
                if cancelled():
                    raise RuntimeError("cancelled")

                call_kwargs = self._build_call_kwargs(pipeline, input_audio_path, prompt, params, str(requested_output))
                _debug_event(
                    "generation.start",
                    prompt=prompt,
                    lyrics=params.get("lyrics", ""),
                    requested_device=requested_device,
                    effective_device=effective_device,
                    checkpoint_path=checkpoint_path,
                    output_path=str(requested_output),
                    pipeline_kwargs=call_kwargs,
                )

                emit("generating", 0.58, f"Generating audio on {effective_device.upper()}")
                result = pipeline(**call_kwargs)

            if cancelled():
                raise RuntimeError("cancelled")

            emit("finalizing", 0.90, "Finalizing generated audio")
            output_file = self._resolve_output_file(result, str(requested_output))
            self._last_error = ""
            self._last_run_details = {
                "mode": "embedded",
                "requested_device": requested_device,
                "effective_device": effective_device,
                "device_id": device_id,
                "checkpoint_path": checkpoint_path,
                "output_path": str(output_file),
                "completed_at": datetime.now(timezone.utc).isoformat(),
            }
            _debug_event(
                "generation.success",
                requested_device=requested_device,
                effective_device=effective_device,
                output_path=str(output_file),
                checkpoint_path=checkpoint_path,
                pipeline_result=result,
            )
            return str(output_file)
        except Exception as exc:
            traceback_text = traceback.format_exc()
            self._last_error = f"embedded: {exc}"
            self._last_run_details = {
                "mode": "embedded",
                "reason": self._last_error,
                "checkpoint_path": checkpoint_path,
                "output_path": str(requested_output),
            }
            _debug_event(
                "generation.failed",
                error=str(exc),
                traceback=traceback_text,
                torch_version=_torch_version(),
                cuda_available=_cuda_available(),
                checkpoint_path=checkpoint_path,
                output_path=str(requested_output),
                requested_device=params.get("device", "auto"),
            )
            raise RuntimeError(self._last_error) from exc
