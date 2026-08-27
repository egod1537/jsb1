from __future__ import annotations

from pathlib import Path, PurePosixPath


class InvalidScenario(ValueError):
    pass


class ScenarioService:
    def __init__(self, scenario_dir: Path) -> None:
        self.scenario_dir = scenario_dir.resolve()

    def list(self) -> list[str]:
        if not self.scenario_dir.is_dir():
            return []
        return sorted(
            path.relative_to(self.scenario_dir).as_posix()
            for path in self.scenario_dir.rglob("*")
            if path.is_file() and path.suffix.lower() in {".yaml", ".yml"}
        )

    def resolve(self, scenario: str) -> Path:
        normalized = PurePosixPath(scenario)
        if normalized.is_absolute() or ".." in normalized.parts:
            raise InvalidScenario("invalid scenario path")
        candidate = (self.scenario_dir / Path(*normalized.parts)).resolve()
        try:
            candidate.relative_to(self.scenario_dir)
        except ValueError as exc:
            raise InvalidScenario("scenario must be inside JSB_SCENARIO_DIR") from exc
        if candidate.suffix.lower() not in {".yaml", ".yml"} or not candidate.is_file():
            raise InvalidScenario(f"unknown scenario: {scenario}")
        return candidate

