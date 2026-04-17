from app.services.add_layer_service import AddLayerService
from app.services.rewrite_service import RewriteService
from app.services.stems_service import StemsService

from fastapi import APIRouter

from app.config import settings


router = APIRouter()
stems_service = StemsService()
rewrite_service = RewriteService()
add_layer_service = AddLayerService()


@router.get("/health")
def health() -> dict:
    return {
        "status": "ok",
        "backend": settings.backend_name,
        "runtime": {
            "stems": stems_service.runtime_summary(),
            "rewrite": rewrite_service.runtime_summary(),
            "add_layer": add_layer_service.runtime_summary(),
        },
    }
