from threading import Lock
from uuid import uuid4

from app.models import JobRecord


class JobStore:
    def __init__(self) -> None:
        self._jobs: dict[str, JobRecord] = {}
        self._lock = Lock()

    def create_job(self, job_type: str) -> JobRecord:
        job = JobRecord(id=str(uuid4()), type=job_type)
        with self._lock:
            self._jobs[job.id] = job
        return job

    def update_job(self, job_id: str, **fields) -> JobRecord | None:
        with self._lock:
            job = self._jobs.get(job_id)
            if job is None:
                return None
            for key, value in fields.items():
                setattr(job, key, value)
            return job

    def get_job(self, job_id: str) -> JobRecord | None:
        with self._lock:
            return self._jobs.get(job_id)

    def request_cancel(self, job_id: str) -> JobRecord | None:
        with self._lock:
            job = self._jobs.get(job_id)
            if job is None:
                return None
            job.cancel_requested = True
            return job

    def cancel_requested(self, job_id: str) -> bool:
        with self._lock:
            job = self._jobs.get(job_id)
            return bool(job and job.cancel_requested)


job_store = JobStore()
