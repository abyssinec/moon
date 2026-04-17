import hashlib
import json
from pathlib import Path


def _hash_file(path: str | None) -> str:
    if not path:
        return ""

    file_path = Path(path)
    if not file_path.exists() or not file_path.is_file():
        return path

    digest = hashlib.sha256()
    with file_path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def compute_cache_key(
    input_audio_path: str,
    reference_audio_path: str | None,
    prompt: str,
    model_name: str,
    params: dict,
) -> str:
    normalized = json.dumps(params, sort_keys=True, separators=(",", ":"))
    payload = "|".join(
        [
            _hash_file(input_audio_path),
            _hash_file(reference_audio_path),
            prompt,
            model_name,
            normalized,
        ]
    )
    return hashlib.sha256(payload.encode("utf-8")).hexdigest()


def output_path(cache_dir: Path, cache_key: str, suffix: str) -> Path:
    cache_dir.mkdir(parents=True, exist_ok=True)
    return cache_dir / f"{cache_key}_{suffix}.wav"


def all_outputs_exist(paths: list[str] | tuple[str, ...]) -> bool:
    return all(Path(path).exists() and Path(path).is_file() for path in paths)
