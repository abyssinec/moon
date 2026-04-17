from pathlib import Path
import shutil
import wave


def ensure_wav_temp(path: str) -> Path:
    output_path = Path(path)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    return output_path


def write_silent_wav(path: str | Path, duration_sec: float, sample_rate: int = 44100, channels: int = 2) -> Path:
    output_path = ensure_wav_temp(str(path))
    frame_count = max(1, int(duration_sec * sample_rate))
    sample_width = 2
    silence_frame = (0).to_bytes(sample_width, byteorder="little", signed=True) * channels

    with wave.open(str(output_path), "wb") as wav_file:
        wav_file.setnchannels(channels)
        wav_file.setsampwidth(sample_width)
        wav_file.setframerate(sample_rate)
        wav_file.writeframes(silence_frame * frame_count)

    return output_path


def wav_duration_seconds(path: str | Path) -> float:
    with wave.open(str(path), "rb") as wav_file:
        frame_rate = wav_file.getframerate()
        frame_count = wav_file.getnframes()
        if frame_rate <= 0:
            return 0.0
        return frame_count / float(frame_rate)


def copy_or_create_wav_like(source_path: str | Path | None, output_path: str | Path, duration_sec: float = 10.0) -> Path:
    destination = ensure_wav_temp(str(output_path))
    if source_path is not None:
        source = Path(source_path)
        if source.exists() and source.suffix.lower() == ".wav":
            shutil.copy2(source, destination)
            return destination

    return write_silent_wav(destination, duration_sec=duration_sec)
