from __future__ import annotations

from functools import lru_cache
from pathlib import Path
from typing import Literal

from pydantic import Field, field_validator
from pydantic_settings import BaseSettings, SettingsConfigDict

PROJECT_ROOT = Path(__file__).resolve().parents[3]
HOST_CONFIG_FILE = Path.home() / ".config" / "jsb1" / "jsb1.env"


class Settings(BaseSettings):
    model_config = SettingsConfigDict(
        env_file=(PROJECT_ROOT / ".env", HOST_CONFIG_FILE),
        extra="ignore",
        populate_by_name=True,
    )

    data_dir: Path = Field(PROJECT_ROOT / "data", alias="JSB1_DATA_DIR")
    database_path: Path | None = Field(None, alias="JSB1_DATABASE_PATH")
    runner_path: Path = Field(Path("jsb-sim-runner"), alias="JSB_SIM_RUNNER_PATH")
    scenario_dir: Path = Field(PROJECT_ROOT / "scenarios", alias="JSB_SCENARIO_DIR")
    managed_scenario_dir: Path | None = Field(
        None, alias="JSB1_MANAGED_SCENARIO_DIR"
    )
    scenario_sftp_host: str | None = Field(None, alias="JSB1_SCENARIO_SFTP_HOST")
    scenario_sftp_port: int = Field(22, alias="JSB1_SCENARIO_SFTP_PORT", ge=1, le=65535)
    scenario_sftp_user: str | None = Field(None, alias="JSB1_SCENARIO_SFTP_USER")
    scenario_sftp_root: str = Field("/", alias="JSB1_SCENARIO_SFTP_ROOT")
    scenario_sftp_key_path: Path | None = Field(None, alias="JSB1_SCENARIO_SFTP_KEY_PATH")
    scenario_sftp_password: str | None = Field(None, alias="JSB1_SCENARIO_SFTP_PASSWORD")
    scenario_sftp_known_hosts_path: Path | None = Field(
        None, alias="JSB1_SCENARIO_SFTP_KNOWN_HOSTS_PATH"
    )
    scenario_sftp_timeout_sec: float = Field(
        15.0, alias="JSB1_SCENARIO_SFTP_TIMEOUT_SEC", gt=0, le=120
    )
    build_branch: str = Field("dev", alias="JSB1_DEPLOY_BRANCH")
    build_commit: str = Field("unknown", alias="JSB1_DEPLOY_COMMIT")
    build_time: str = Field("unknown", alias="JSB1_DEPLOY_BUILT_AT")
    build_hostname: str | None = Field(None, alias="JSB1_DEPLOY_HOSTNAME")
    max_concurrent_runs: int = Field(1, alias="JSB1_MAX_CONCURRENT_RUNS", ge=1, le=32)
    execution_mode: Literal["embedded", "external"] = Field(
        "embedded", alias="JSB1_EXECUTION_MODE"
    )
    worker_poll_interval_sec: float = Field(
        0.5, alias="JSB1_WORKER_POLL_INTERVAL_SEC", gt=0, le=60
    )
    run_timeout_sec: float = Field(1800.0, alias="JSB1_RUN_TIMEOUT_SEC", gt=0)
    repository_root: Path | None = Field(None, alias="JSB1_REPOSITORY_ROOT")
    # JSB1 manages exactly one canonical JSB0 Runtime repository.
    jsb0_repository_url: str = Field(
        "https://github.com/egod1537/jsb0.git",
        alias="JSB0_REPOSITORY_URL",
        min_length=1,
        max_length=2048,
    )
    jsb0_repository_path: Path | None = Field(None, alias="JSB0_REPOSITORY_PATH")
    jsb0_default_branch: str = Field(
        "impl", alias="JSB0_DEFAULT_BRANCH", min_length=1, max_length=255
    )
    bootstrap_runtime_repository: bool = Field(
        True, alias="JSB1_BOOTSTRAP_RUNTIME_REPOSITORY"
    )
    worktree_root: Path | None = Field(None, alias="JSB1_WORKTREE_ROOT")
    deployment_root: Path | None = Field(None, alias="JSB1_DEPLOYMENT_ROOT")
    deployment_port_start: int = Field(
        8100, alias="JSB1_DEPLOYMENT_PORT_START", ge=1024, le=65534
    )
    deployment_port_end: int = Field(
        8999, alias="JSB1_DEPLOYMENT_PORT_END", ge=1025, le=65535
    )
    deployment_health_timeout_sec: float = Field(
        120.0, alias="JSB1_DEPLOYMENT_HEALTH_TIMEOUT_SEC", gt=0, le=900
    )
    deployment_health_interval_sec: float = Field(
        2.0, alias="JSB1_DEPLOYMENT_HEALTH_INTERVAL_SEC", gt=0, le=60
    )
    deployment_https_port: int = Field(
        443, alias="JSB1_DEPLOYMENT_HTTPS_PORT", ge=1, le=65535
    )
    deployment_health_host: str = Field(
        "127.0.0.1", alias="JSB1_DEPLOYMENT_HEALTH_HOST"
    )
    deployment_port_probe_host: str = Field(
        "127.0.0.1", alias="JSB1_DEPLOYMENT_PORT_PROBE_HOST"
    )
    deployment_command_timeout_sec: float = Field(
        1800.0, alias="JSB1_DEPLOYMENT_COMMAND_TIMEOUT_SEC", gt=0
    )
    deployment_base_domain: str = Field(
        "mangagaki.net", alias="JSB1_DEPLOYMENT_BASE_DOMAIN"
    )
    deployment_main_hostname: str = Field(
        "jsb.mangagaki.net", alias="JSB1_DEPLOYMENT_MAIN_HOSTNAME"
    )
    deployment_main_branch: str = Field("main", alias="JSB1_DEPLOYMENT_MAIN_BRANCH")
    tls_cert_path: Path | None = Field(None, alias="JSB1_TLS_CERT_PATH")
    tls_key_path: Path | None = Field(None, alias="JSB1_TLS_KEY_PATH")
    caddy_config_path: Path | None = Field(None, alias="JSB1_CADDY_CONFIG_PATH")
    caddy_fragments_dir: Path | None = Field(None, alias="JSB1_CADDY_FRAGMENTS_DIR")
    caddy_binary: str = Field("caddy", alias="JSB1_CADDY_BINARY")
    caddy_container: str | None = Field(None, alias="JSB1_CADDY_CONTAINER")
    caddy_upstream_host: str = Field(
        "127.0.0.1", alias="JSB1_CADDY_UPSTREAM_HOST"
    )
    caddy_health_host: str = Field("127.0.0.1", alias="JSB1_CADDY_HEALTH_HOST")
    docker_binary: str = Field("docker", alias="JSB1_DOCKER_BINARY")
    build_root: Path | None = Field(None, alias="JSB1_BUILD_ROOT")
    max_concurrent_builds: int = Field(
        1, alias="JSB1_MAX_CONCURRENT_BUILDS", ge=1, le=16
    )
    build_jobs: int = Field(2, alias="JSB1_BUILD_JOBS", ge=1, le=64)
    build_timeout_sec: float = Field(3600.0, alias="JSB1_BUILD_TIMEOUT_SEC", gt=0)
    build_executable_relative_path: Path = Field(
        Path("jsb-sim-runner"), alias="JSB1_BUILD_EXECUTABLE_RELATIVE_PATH"
    )
    cors_origins: list[str] = Field(
        default_factory=lambda: ["http://localhost:5173", "http://127.0.0.1:5173"],
        alias="JSB1_CORS_ORIGINS",
    )

    @field_validator("cors_origins", mode="before")
    @classmethod
    def parse_origins(cls, value: object) -> object:
        if isinstance(value, str) and not value.lstrip().startswith("["):
            return [item.strip() for item in value.split(",") if item.strip()]
        return value

    @field_validator(
        "managed_scenario_dir",
        "scenario_sftp_host",
        "scenario_sftp_user",
        "scenario_sftp_key_path",
        "scenario_sftp_password",
        "scenario_sftp_known_hosts_path",
        mode="before",
    )
    @classmethod
    def empty_scenario_values_are_unset(cls, value: object) -> object:
        if isinstance(value, str) and not value.strip():
            return None
        return value

    @property
    def resolved_database_path(self) -> Path:
        return self.database_path or self.data_dir / "jsb1.db"

    @property
    def runs_dir(self) -> Path:
        return self.data_dir / "runs"

    @property
    def remote_scenario_cache_dir(self) -> Path:
        return self.data_dir / "scenarios" / "remote"

    @property
    def resolved_managed_scenario_dir(self) -> Path:
        if self.managed_scenario_dir is None:
            return self.data_dir / "scenarios" / "managed"
        return self.managed_scenario_dir.expanduser().resolve()

    @property
    def resolved_repository_root(self) -> Path:
        return self.repository_root or self.data_dir / "repositories"

    @property
    def resolved_worktree_root(self) -> Path:
        return self.worktree_root or self.data_dir / "worktrees"

    @property
    def resolved_jsb0_repository_path(self) -> Path:
        if self.jsb0_repository_path is None:
            return (self.resolved_repository_root / "jsb0").resolve()
        configured = self.jsb0_repository_path.expanduser()
        if configured.is_absolute():
            return configured.resolve()
        return (PROJECT_ROOT / configured).resolve()

    @property
    def resolved_deployment_root(self) -> Path:
        return self.deployment_root or self.data_dir / "deployments"

    @property
    def resolved_caddy_fragments_dir(self) -> Path:
        return self.caddy_fragments_dir or self.data_dir / "caddy" / "deployments"

    @property
    def resolved_caddy_config_path(self) -> Path:
        return self.caddy_config_path or self.data_dir / "caddy" / "Caddyfile"

    @property
    def resolved_build_root(self) -> Path:
        return self.build_root or self.data_dir / "builds"


@lru_cache
def get_settings() -> Settings:
    return Settings()
