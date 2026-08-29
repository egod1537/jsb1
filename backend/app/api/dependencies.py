from fastapi import Request

from app.analysis.mcap_reader import McapRunReader
from app.config.settings import Settings
from app.repositories.database import Database
from app.repositories.runs import RunRepository
from app.repositories.builds import BuildRepository
from app.repositories.instances import InstanceRepository
from app.repositories.deployments import DeploymentRepository
from app.repositories.jsb_repository_repository import JsbRepositoryRepository
from app.services.build_manager import BuildManager
from app.services.repository_manager import RepositoryManager
from app.services.deployment_manager import DeploymentManager
from app.services.artifacts import ArtifactService
from app.services.scenarios import ScenarioService
from app.workers.scheduler import InProcessRunScheduler
from app.workers.build_scheduler import InProcessBuildScheduler


def get_database(request: Request) -> Database:
    return request.app.state.database


def get_repository(request: Request) -> RunRepository:
    return request.app.state.repository


def get_jsb_repositories(request: Request) -> JsbRepositoryRepository:
    return request.app.state.jsb_repositories


def get_repository_manager(request: Request) -> RepositoryManager:
    return request.app.state.repository_manager


def get_builds(request: Request) -> BuildRepository:
    return request.app.state.builds


def get_build_manager(request: Request) -> BuildManager:
    return request.app.state.build_manager


def get_build_scheduler(request: Request) -> InProcessBuildScheduler:
    return request.app.state.build_scheduler


def get_instances(request: Request) -> InstanceRepository:
    return request.app.state.instances


def get_deployments(request: Request) -> DeploymentRepository:
    return request.app.state.deployments


def get_deployment_manager(request: Request) -> DeploymentManager:
    return request.app.state.deployment_manager


def get_scenarios(request: Request) -> ScenarioService:
    return request.app.state.scenarios


def get_scheduler(request: Request) -> InProcessRunScheduler:
    return request.app.state.scheduler


def get_reader(request: Request) -> McapRunReader:
    return request.app.state.reader


def get_artifact_service(request: Request) -> ArtifactService:
    return request.app.state.artifacts


def get_app_settings(request: Request) -> Settings:
    return request.app.state.settings
