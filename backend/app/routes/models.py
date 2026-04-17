from fastapi import APIRouter

from app.services.add_layer_service import AddLayerService
from app.services.rewrite_service import RewriteService
from app.services.stems_service import StemsService


router = APIRouter()
stems_service = StemsService()
rewrite_service = RewriteService()
add_layer_service = AddLayerService()


@router.get("/models")
def models() -> dict:
    return {
        "stems": stems_service.available_models(),
        "rewrite": rewrite_service.available_models(),
        "add_layer": add_layer_service.available_models(),
        "details": {
            "stems": stems_service.runtime_summary(),
            "rewrite": rewrite_service.runtime_summary(),
            "add_layer": add_layer_service.runtime_summary(),
        },
    }
