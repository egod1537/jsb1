from __future__ import annotations

import argparse
from collections.abc import Sequence
from dataclasses import dataclass
from pathlib import Path
from typing import TextIO

from app.domain.scenario_validation import ScenarioValidationPolicy
from app.infrastructure.git import GitOperationError, GitRepositoryAdapter
from app.services.scenario_validator import (
    ScenarioValidationUnavailable,
    ScenarioValidator,
)
from app.services.scenarios import ScenarioService


@dataclass(frozen=True)
class ValidationSummary:
    runtime_commit: str
    validated: int
    passed: int
    failed: int

    @property
    def exit_code(self) -> int:
        return 1 if self.failed else 0


def _display_path(path: Path) -> str:
    try:
        return path.resolve().relative_to(Path.cwd().resolve()).as_posix()
    except ValueError:
        return path.as_posix()


def _runtime_commit(runtime_root: Path) -> str:
    try:
        return GitRepositoryAdapter().git(
            runtime_root,
            ["rev-parse", "HEAD"],
            operation="resolve validation runtime revision",
        )
    except GitOperationError:
        return "unknown"


def _runtime_branch(runtime_root: Path) -> str:
    try:
        branch = GitRepositoryAdapter().git(
            runtime_root,
            ["branch", "--show-current"],
            operation="resolve validation runtime branch",
        )
        return branch or "detached"
    except GitOperationError:
        return "unknown"


def validate_directory(
    scenario_dir: Path,
    runtime_root: Path,
    output: TextIO,
) -> ValidationSummary:
    """Validate every bundled YAML scenario with one JSB0 contract checkout."""
    service = ScenarioService(scenario_dir)
    scenarios = service.list()
    runtime_commit = _runtime_commit(runtime_root)
    runtime_branch = _runtime_branch(runtime_root)

    if not scenarios:
        print(f"FAIL {_display_path(scenario_dir)}", file=output)
        print("  no .yaml or .yml scenario files found", file=output)
        print(file=output)
        print("Validated: 0", file=output)
        print("Passed: 0", file=output)
        print("Failed: 1", file=output)
        return ValidationSummary(runtime_commit, 0, 0, 1)

    scenario_validator = ScenarioValidator()
    try:
        contract = scenario_validator.load_runtime_contract(runtime_root)
    except ScenarioValidationUnavailable as exc:
        print("FAIL JSB0 scenario contract", file=output)
        print(f"  {exc}", file=output)
        print(file=output)
        print(f"Validated: {len(scenarios)}", file=output)
        print("Passed: 0", file=output)
        print(f"Failed: {len(scenarios)}", file=output)
        return ValidationSummary(runtime_commit, len(scenarios), 0, len(scenarios))

    passed = 0
    failed = 0
    for scenario in scenarios:
        path = scenario_dir / scenario
        result = scenario_validator.validate_file(
            path,
            contract,
            runtime_branch=runtime_branch,
            runtime_commit=runtime_commit,
            policy=ScenarioValidationPolicy.CATALOG_STABLE,
        )
        errors: list[str] = []
        for error in result.errors:
            if error.path == "autopilot" and error.code == "required":
                errors.append("Scenario does not define an autopilot.")
            elif error.path in {"autopilot", "autopilot.type"} and error.code == "enum":
                errors.append(
                    f"Scenario requires unsupported autopilot "
                    f"'{result.scenario.autopilot if result.scenario else 'unknown'}'."
                )
            else:
                errors.append(
                    f"{error.path}: {error.message}"
                    if error.path != "$"
                    else error.message
                )

        if errors:
            failed += 1
            print(f"FAIL {_display_path(path)}", file=output)
            for error in errors:
                print(f"  {error}", file=output)
        else:
            passed += 1
            print(f"PASS {_display_path(path)}", file=output)

    print(file=output)
    print(f"Validated: {len(scenarios)}", file=output)
    print(f"Passed: {passed}", file=output)
    print(f"Failed: {failed}", file=output)
    if not failed:
        print(
            f"Validated {len(scenarios)} scenarios against the exact JSB0 checkout",
            file=output,
        )
        print(f"JSB0 commit: {runtime_commit}", file=output)
        print("PASS: all bundled scenarios compatible", file=output)

    return ValidationSummary(runtime_commit, len(scenarios), passed, failed)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Validate bundled JSB1 scenarios against a JSB0 checkout."
    )
    parser.add_argument("--scenario-dir", type=Path, required=True)
    parser.add_argument("--runtime-root", type=Path, required=True)
    return parser


def main(argv: Sequence[str] | None = None, output: TextIO | None = None) -> int:
    import sys

    args = build_parser().parse_args(argv)
    summary = validate_directory(
        args.scenario_dir,
        args.runtime_root,
        output or sys.stdout,
    )
    return summary.exit_code
