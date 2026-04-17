from fastapi import FastAPI

from app.routes.health import router as health_router
from app.routes.jobs import router as jobs_router
from app.routes.models import router as models_router


app = FastAPI(title="local-ai-audio-service", version="0.1.0")
app.include_router(health_router)
app.include_router(models_router)
app.include_router(jobs_router)
