from fastapi import Request

from app.analysis.mcap_reader import McapRunReader
from app.config.settings import Settings
from app.domain.build_info import BuildInfo
from app.repositories.builds import BuildRepository
from app.repositories.comparisons import ComparisonRepository
from app.repositories.database import Database
from app.repositories.deployments import DeploymentRepository
from app.repositories.instances import InstanceRepository
from app.repositories.jsb_repository_repository import JsbRepositoryRepository
from app.repositories.runs import RunRepository
from app.services.artifacts import ArtifactService
from app.services.build_manager import BuildManager
from app.services.deployment_manager import DeploymentManager
from app.services.repository_manager import RepositoryManager
from app.services.scenario_sync import (
    ScenarioCompatibilityResolver,
    ScenarioSyncService,
)
from app.services.scenario_inspection import ScenarioInspectionService
from app.services.scenario_validator import ScenarioValidator
from app.services.scenario_writes import ScenarioWriteService
from app.services.scenarios import ScenarioService
from app.services.runtime_variants import RuntimeVariantService
from app.services.run_creation import RunCreationService
from app.services.run_analysis import RunAnalysisService
from app.services.run_deletion import RunDeletionService
from app.services.comparison_creation import ComparisonCreationService
from app.services.controller_parameters import RuntimeControllerParameterService
from app.services.telemetry_queries import TelemetryQueryService
from app.workers.dispatch import BuildDispatcher, RunDispatcher


def get_database(request: Request) -> Database:
    return request.app.state.database


def get_repository(request: Request) -> RunRepository:
    return request.app.state.repository


def get_comparisons(request: Request) -> ComparisonRepository:
    return request.app.state.comparisons


def get_jsb_repositories(request: Request) -> JsbRepositoryRepository:
    return request.app.state.jsb_repositories


def get_repository_manager(request: Request) -> RepositoryManager:
    return request.app.state.repository_manager


def get_builds(request: Request) -> BuildRepository:
    return request.app.state.builds


def get_build_manager(request: Request) -> BuildManager:
    return request.app.state.build_manager


def get_build_scheduler(request: Request) -> BuildDispatcher:
    return request.app.state.build_scheduler


def get_instances(request: Request) -> InstanceRepository:
    return request.app.state.instances


def get_deployments(request: Request) -> DeploymentRepository:
    return request.app.state.deployments


def get_deployment_manager(request: Request) -> DeploymentManager:
    return request.app.state.deployment_manager


def get_scenarios(request: Request) -> ScenarioService:
    return request.app.state.scenarios


def get_runtime_variants(request: Request) -> RuntimeVariantService:
    return request.app.state.runtime_variants


def get_controller_parameters(request: Request) -> RuntimeControllerParameterService:
    return request.app.state.controller_parameters


def get_run_creation(request: Request) -> RunCreationService:
    return request.app.state.run_creation


def get_run_analysis(request: Request) -> RunAnalysisService:
    return request.app.state.run_analysis


def get_run_deletion(request: Request) -> RunDeletionService:
    return request.app.state.run_deletion


def get_comparison_creation(request: Request) -> ComparisonCreationService:
    return request.app.state.comparison_creation


def get_scenario_validator(request: Request) -> ScenarioValidator:
    return request.app.state.scenario_validator


def get_scenario_compatibility(request: Request) -> ScenarioCompatibilityResolver:
    return request.app.state.scenario_compatibility


def get_scenario_sync(request: Request) -> ScenarioSyncService:
    return request.app.state.scenario_sync


def get_scenario_writer(request: Request) -> ScenarioWriteService:
    return request.app.state.scenario_writer


def get_scenario_inspector(request: Request) -> ScenarioInspectionService:
    return request.app.state.scenario_inspector


def get_scheduler(request: Request) -> RunDispatcher:
    return request.app.state.scheduler


def get_reader(request: Request) -> McapRunReader:
    return request.app.state.reader


def get_telemetry_queries(request: Request) -> TelemetryQueryService:
    return request.app.state.telemetry_queries


def get_artifact_service(request: Request) -> ArtifactService:
    return request.app.state.artifacts


def get_app_settings(request: Request) -> Settings:
    return request.app.state.settings


def get_build_info(request: Request) -> BuildInfo:
    return request.app.state.build_info
