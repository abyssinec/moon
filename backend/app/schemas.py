from pydantic import BaseModel, Field


class HealthResponse(BaseModel):
    status: str
    backend: str


class ModelsResponse(BaseModel):
    stems: list[str]
    rewrite: list[str]
    add_layer: list[str] = Field(alias="add_layer")


class StemsJobRequest(BaseModel):
    input_audio_path: str
    model: str = "demucs"


class RewriteJobRequest(BaseModel):
    input_audio_path: str
    reference_audio_path: str | None = None
    prompt: str
    model: str = "ace_step_stub"
    strength: float = 0.55
    preserve_timing: bool = True
    preserve_melody: bool = True
    seed: int = 0
    duration_sec: float = 0.0


class AddLayerJobRequest(BaseModel):
    input_audio_path: str
    prompt: str
    model: str = "ace_step_stub"
    seed: int = 0
    duration_sec: float = 0.0
