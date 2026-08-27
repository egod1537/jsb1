from __future__ import annotations

from functools import lru_cache
from pathlib import Path

from pydantic import Field, field_validator
from pydantic_settings import BaseSettings, SettingsConfigDict


PROJECT_ROOT = Path(__file__).resolve().parents[3]


class Settings(BaseSettings):
    model_config = SettingsConfigDict(
        env_file=PROJECT_ROOT / ".env", extra="ignore", populate_by_name=True
    )

    data_dir: Path = Field(PROJECT_ROOT / "data", alias="JSB1_DATA_DIR")
    database_path: Path | None = Field(None, alias="JSB1_DATABASE_PATH")
    runner_path: Path = Field(Path("jsb-sim-runner"), alias="JSB_SIM_RUNNER_PATH")
    scenario_dir: Path = Field(PROJECT_ROOT / "scenarios", alias="JSB_SCENARIO_DIR")
    max_concurrent_runs: int = Field(1, alias="JSB1_MAX_CONCURRENT_RUNS", ge=1, le=32)
    run_timeout_sec: float = Field(1800.0, alias="JSB1_RUN_TIMEOUT_SEC", gt=0)
    autopilots: list[str] = Field(default_factory=lambda: ["primary"], alias="JSB1_AUTOPILOTS")
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

    @field_validator("autopilots", mode="before")
    @classmethod
    def parse_autopilots(cls, value: object) -> object:
        if isinstance(value, str) and not value.lstrip().startswith("["):
            return [item.strip() for item in value.split(",") if item.strip()]
        return value

    @property
    def resolved_database_path(self) -> Path:
        return self.database_path or self.data_dir / "jsb1.db"

    @property
    def runs_dir(self) -> Path:
        return self.data_dir / "runs"


@lru_cache
def get_settings() -> Settings:
    return Settings()
