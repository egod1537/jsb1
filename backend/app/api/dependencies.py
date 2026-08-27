from fastapi import Request

from app.analysis.mcap_reader import McapRunReader
from app.config.settings import Settings
from app.repositories.database import Database
from app.repositories.runs import RunRepository
from app.services.artifacts import ArtifactService
from app.services.scenarios import ScenarioService
from app.workers.scheduler import InProcessRunScheduler


def get_database(request: Request) -> Database:
    return request.app.state.database


def get_repository(request: Request) -> RunRepository:
    return request.app.state.repository


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

