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
from app.repositories.builds import BuildRepository
from app.repositories.instances import InstanceRepository
from app.repositories.deployments import DeploymentRepository
from app.repositories.jsb_repository_repository import JsbRepositoryRepository
from app.services.artifacts import ArtifactService
from app.services.execution import RunExecutionService
from app.services.runner import ExternalSimulationRunner, SimulationRunner
from app.services.build_manager import BuildManager
from app.services.build_info import load_build_info
from app.services.repository_manager import RepositoryManager
from app.services.deployment_manager import DeploymentManager
from app.services.scenarios import ScenarioService
from app.workers.scheduler import InProcessRunScheduler
from app.workers.build_scheduler import InProcessBuildScheduler


logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s %(levelname)s %(name)s %(message)s",
)


def create_app(
    settings: Settings | None = None,
    runner_override: SimulationRunner | None = None,
) -> FastAPI:
    app_settings = settings or get_settings()
    build_info = load_build_info(app_settings)

    @asynccontextmanager
    async def lifespan(app: FastAPI) -> AsyncIterator[None]:
        app_settings.data_dir.mkdir(parents=True, exist_ok=True)
        app_settings.runs_dir.mkdir(parents=True, exist_ok=True)
        app_settings.resolved_repository_root.mkdir(parents=True, exist_ok=True)
        app_settings.resolved_worktree_root.mkdir(parents=True, exist_ok=True)
        app_settings.resolved_build_root.mkdir(parents=True, exist_ok=True)
        app_settings.resolved_deployment_root.mkdir(parents=True, exist_ok=True)
        app_settings.resolved_caddy_fragments_dir.mkdir(parents=True, exist_ok=True)
        migrations = Path(__file__).resolve().parents[1] / "migrations"
        database = Database(app_settings.resolved_database_path, migrations)
        database.initialize()
        repository = RunRepository(database)
        jsb_repositories = JsbRepositoryRepository(database)
        builds = BuildRepository(database)
        instances = InstanceRepository(database)
        deployments = DeploymentRepository(database)
        recovered = repository.fail_incomplete_from_previous_process()
        if recovered:
            logging.getLogger(__name__).warning(
                "marked %s interrupted runs as failed during startup", recovered
            )
        recovered_builds = builds.fail_incomplete_from_previous_process()
        recovered_instances = instances.fail_incomplete_from_previous_process()
        recovered_deployments = deployments.fail_interrupted()
        if recovered_builds or recovered_instances or recovered_deployments:
            logging.getLogger(__name__).warning(
                "recovered interrupted builds=%s instances=%s deployments=%s",
                recovered_builds,
                recovered_instances,
                recovered_deployments,
            )
        repository_manager = RepositoryManager(
            jsb_repositories,
            app_settings.resolved_repository_root,
            app_settings.resolved_worktree_root,
        )
        build_manager = BuildManager(
            builds,
            repository_manager,
            app_settings.resolved_build_root,
            executable_relative_path=app_settings.build_executable_relative_path,
            build_jobs=app_settings.build_jobs,
            timeout_sec=app_settings.build_timeout_sec,
        )
        deployment_manager = DeploymentManager(
            deployments,
            repository_manager,
            app_settings,
        )
        build_scheduler = InProcessBuildScheduler(
            build_manager, app_settings.max_concurrent_builds
        )
        reader = McapRunReader()
        runner = runner_override or ExternalSimulationRunner(
            app_settings.runner_path, app_settings.run_timeout_sec
        )
        execution = RunExecutionService(
            repository,
            runner,
            reader,
            app_settings.data_dir,
            builds,
            instances,
            app_settings.resolved_build_root,
        )
        scheduler = InProcessRunScheduler(
            execution, app_settings.max_concurrent_runs, build_scheduler
        )
        app.state.settings = app_settings
        app.state.build_info = build_info
        app.state.database = database
        app.state.repository = repository
        app.state.jsb_repositories = jsb_repositories
        app.state.repository_manager = repository_manager
        app.state.builds = builds
        app.state.instances = instances
        app.state.deployments = deployments
        app.state.deployment_manager = deployment_manager
        app.state.build_manager = build_manager
        app.state.build_scheduler = build_scheduler
        app.state.reader = reader
        app.state.scenarios = ScenarioService(app_settings.scenario_dir)
        app.state.artifacts = ArtifactService(app_settings.data_dir)
        app.state.scheduler = scheduler
        yield
        await scheduler.shutdown()
        await build_scheduler.shutdown()
        await deployment_manager.shutdown()

    app = FastAPI(title="JSB1", version="0.1.0", lifespan=lifespan)
    app.add_middleware(
        CORSMiddleware,
        allow_origins=app_settings.cors_origins,
        allow_credentials=False,
        allow_methods=["GET", "POST", "DELETE"],
        allow_headers=["Content-Type"],
    )
    app.include_router(router)
    return app


app = create_app()
