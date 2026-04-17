from dataclasses import dataclass, field
from typing import Any


@dataclass
class JobRecord:
    id: str
    type: str
    status: str = "queued"
    progress: float = 0.0
    message: str = "queued"
    details: dict[str, Any] = field(default_factory=dict)
    result: dict[str, Any] | None = field(default=None)
