from __future__ import annotations

import logging
from contextlib import asynccontextmanager
from pathlib import Path
from typing import AsyncIterator

from fastapi import FastAPI
from fastapi.middleware.cors import CORSMiddleware

from app.analysis.mcap_reader import McapRunReader
from app.api.routes import router
from app.config.settings import Settings, get_settings
from app.repositories.database import Database
from app.repositories.runs import RunRepository
from app.services.artifacts import ArtifactService
from app.services.execution import RunExecutionService
from app.services.runner import ExternalSimulationRunner, SimulationRunner
from app.services.scenarios import ScenarioService
from app.workers.scheduler import InProcessRunScheduler


logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s %(levelname)s %(name)s %(message)s",
)


def create_app(
    settings: Settings | None = None,
    runner_override: SimulationRunner | None = None,
) -> FastAPI:
    app_settings = settings or get_settings()

    @asynccontextmanager
    async def lifespan(app: FastAPI) -> AsyncIterator[None]:
        app_settings.data_dir.mkdir(parents=True, exist_ok=True)
        app_settings.runs_dir.mkdir(parents=True, exist_ok=True)
        migrations = Path(__file__).resolve().parents[1] / "migrations"
        database = Database(app_settings.resolved_database_path, migrations)
        database.initialize()
        repository = RunRepository(database)
        recovered = repository.fail_incomplete_from_previous_process()
        if recovered:
            logging.getLogger(__name__).warning(
                "marked %s interrupted runs as failed during startup", recovered
            )
        reader = McapRunReader()
        runner = runner_override or ExternalSimulationRunner(
            app_settings.runner_path, app_settings.run_timeout_sec
        )
        execution = RunExecutionService(
            repository, runner, reader, app_settings.data_dir
        )
        scheduler = InProcessRunScheduler(execution, app_settings.max_concurrent_runs)
        app.state.settings = app_settings
        app.state.database = database
        app.state.repository = repository
        app.state.reader = reader
        app.state.scenarios = ScenarioService(app_settings.scenario_dir)
        app.state.artifacts = ArtifactService(app_settings.data_dir)
        app.state.scheduler = scheduler
        yield
        await scheduler.shutdown()

    app = FastAPI(title="JSB1", version="0.1.0", lifespan=lifespan)
    app.add_middleware(
        CORSMiddleware,
        allow_origins=app_settings.cors_origins,
        allow_credentials=False,
        allow_methods=["GET", "POST"],
        allow_headers=["Content-Type"],
    )
    app.include_router(router)
    return app


app = create_app()
