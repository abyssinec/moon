from fastapi import APIRouter, BackgroundTasks, HTTPException

from app.cache_manager import all_outputs_exist, compute_cache_key, output_path
from app.config import settings
from app.job_store import job_store
from app.schemas import AddLayerJobRequest, RewriteJobRequest, StemsJobRequest
from app.services.add_layer_service import AddLayerService
from app.services.rewrite_service import RewriteService
from app.services.stems_service import StemsService


router = APIRouter(prefix="/jobs")
stems_service = StemsService()
rewrite_service = RewriteService()
add_layer_service = AddLayerService()


def _run_stems_job(job_id: str, request: StemsJobRequest) -> None:
    try:
        key = compute_cache_key(request.input_audio_path, None, "", request.model, {})
        outputs = {
            "vocals": str(output_path(settings.cache_dir, key, "vocals")),
            "drums": str(output_path(settings.cache_dir, key, "drums")),
            "bass": str(output_path(settings.cache_dir, key, "bass")),
            "other": str(output_path(settings.cache_dir, key, "other")),
        }
        runtime_details = stems_service.runtime_summary()
        if all_outputs_exist(tuple(outputs.values())):
            job_store.update_job(
                job_id,
                status="completed",
                progress=1.0,
                message="cache hit",
                details={"cache_hit": True, "runner": runtime_details},
                result={"outputs": outputs, "details": {"cache_hit": True, "runner": runtime_details}},
            )
            return

        job_store.update_job(
            job_id,
            status="running",
            progress=0.2,
            message="preparing stems",
            details={"cache_hit": False, "stage": "preparing", "runner": runtime_details},
        )
        resolved_outputs = stems_service.run_stems(request.input_audio_path, request.model, outputs)
        job_store.update_job(
            job_id,
            status="running",
            progress=0.9,
            message="finalizing stems",
            details={"cache_hit": False, "stage": "finalizing", "runner": stems_service.runtime_summary()},
        )
        job_store.update_job(
            job_id,
            status="completed",
            progress=1.0,
            message="completed",
            details={"cache_hit": False, "stage": "completed", "runner": stems_service.runtime_summary()},
            result={
                "outputs": resolved_outputs,
                "details": {"cache_hit": False, "stage": "completed", "runner": stems_service.runtime_summary()},
            },
        )
    except Exception as exc:
        job_store.update_job(
            job_id,
            status="failed",
            progress=1.0,
            message=str(exc),
            details={"cache_hit": False, "runner": stems_service.runtime_summary()},
        )


def _run_single_output_job(job_id: str, job_type: str, input_path: str, prompt: str, model: str, params: dict) -> None:
    try:
        key = compute_cache_key(input_path, params.get("reference_audio_path"), prompt, model, params)
        result_path = output_path(settings.cache_dir, key, job_type)
        service = rewrite_service if job_type == "rewrite" else add_layer_service
        runtime_details = service.runtime_summary()
        if all_outputs_exist((str(result_path),)):
            job_store.update_job(
                job_id,
                status="completed",
                progress=1.0,
                message="cache hit",
                details={"cache_hit": True, "runner": runtime_details},
                result={
                    "output_audio_path": str(result_path),
                    "details": {"cache_hit": True, "runner": runtime_details},
                },
            )
            return

        job_store.update_job(
            job_id,
            status="running",
            progress=0.2,
            message=f"{job_type} preparing",
            details={"cache_hit": False, "stage": "preparing", "runner": runtime_details},
        )
        if job_type == "rewrite":
            job_store.update_job(
                job_id,
                status="running",
                progress=0.6,
                message="rewrite processing",
                details={"cache_hit": False, "stage": "processing", "runner": runtime_details},
            )
            output_audio_path = rewrite_service.run_rewrite(
                input_path,
                params.get("reference_audio_path"),
                prompt,
                params,
                str(result_path),
            )
        else:
            job_store.update_job(
                job_id,
                status="running",
                progress=0.6,
                message="add-layer processing",
                details={"cache_hit": False, "stage": "processing", "runner": runtime_details},
            )
            output_audio_path = add_layer_service.run_add_layer(
                input_path,
                prompt,
                params,
                str(result_path),
            )
        final_runtime_details = service.runtime_summary()
        job_store.update_job(
            job_id,
            status="running",
            progress=0.9,
            message=f"{job_type} finalizing",
            details={"cache_hit": False, "stage": "finalizing", "runner": final_runtime_details},
        )
        job_store.update_job(
            job_id,
            status="completed",
            progress=1.0,
            message="completed",
            details={"cache_hit": False, "stage": "completed", "runner": final_runtime_details},
            result={
                "output_audio_path": str(output_audio_path),
                "details": {"cache_hit": False, "stage": "completed", "runner": final_runtime_details},
            },
        )
    except Exception as exc:
        service = rewrite_service if job_type == "rewrite" else add_layer_service
        job_store.update_job(
            job_id,
            status="failed",
            progress=1.0,
            message=str(exc),
            details={"cache_hit": False, "runner": service.runtime_summary()},
        )


@router.post("/stems")
def create_stems_job(request: StemsJobRequest, background_tasks: BackgroundTasks) -> dict[str, str]:
    job = job_store.create_job("stems")
    background_tasks.add_task(_run_stems_job, job.id, request)
    return {"id": job.id, "status": job.status}


@router.post("/rewrite")
def create_rewrite_job(request: RewriteJobRequest, background_tasks: BackgroundTasks) -> dict[str, str]:
    job = job_store.create_job("rewrite")
    background_tasks.add_task(
        _run_single_output_job,
        job.id,
        "rewrite",
        request.input_audio_path,
        request.prompt,
        request.model,
        request.model_dump(),
    )
    return {"id": job.id, "status": job.status}


@router.post("/add-layer")
def create_add_layer_job(request: AddLayerJobRequest, background_tasks: BackgroundTasks) -> dict[str, str]:
    job = job_store.create_job("add-layer")
    background_tasks.add_task(
        _run_single_output_job,
        job.id,
        "add_layer",
        request.input_audio_path,
        request.prompt,
        request.model,
        request.model_dump(),
    )
    return {"id": job.id, "status": job.status}


@router.get("/{job_id}")
def get_job(job_id: str) -> dict:
    job = job_store.get_job(job_id)
    if job is None:
        raise HTTPException(status_code=404, detail="job not found")
    return {
        "id": job.id,
        "type": job.type,
        "status": job.status,
        "progress": job.progress,
        "message": job.message,
        "details": job.details,
    }


@router.get("/{job_id}/result")
def get_job_result(job_id: str) -> dict:
    job = job_store.get_job(job_id)
    if job is None:
        raise HTTPException(status_code=404, detail="job not found")
    if job.status != "completed" or job.result is None:
        raise HTTPException(status_code=409, detail="job not completed")
    return {"id": job.id, "status": job.status, **job.result}
