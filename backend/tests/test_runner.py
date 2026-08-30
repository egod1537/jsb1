from __future__ import annotations

from pathlib import Path

import pytest

from app.services.runner import ExternalSimulationRunner


@pytest.mark.asyncio
async def test_external_runner_uses_compare_only_contract(
    tmp_path: Path,
) -> None:
    captured = tmp_path / "arguments.txt"
    executable = tmp_path / "jsb-sim-runner"
    executable.write_text(
        f"#!/bin/sh\nprintf '%s\\n' \"$@\" > '{captured}'\n",
        encoding="utf-8",
    )
    executable.chmod(0o755)
    scenario = tmp_path / "scenario.yaml"
    scenario.write_text("autopilot: primary\n", encoding="utf-8")
    output = tmp_path / "output"
    output.mkdir()

    result = await ExternalSimulationRunner(executable, 5).run(
        scenario_path=scenario,
        output_directory=output,
        log_path=output / "stdout.log",
    )

    assert result.exit_code == 0
    assert captured.read_text(encoding="utf-8").splitlines() == [
        "--scenario",
        str(scenario),
        "--output",
        str(output),
    ]


@pytest.mark.asyncio
async def test_branch_built_runner_uses_build_directory_as_cwd(tmp_path: Path) -> None:
    build_dir = tmp_path / "build"
    build_dir.mkdir()
    captured = tmp_path / "cwd.txt"
    executable = build_dir / "jsb-sim-runner"
    executable.write_text(
        f"#!/bin/sh\npwd > '{captured}'\n",
        encoding="utf-8",
    )
    executable.chmod(0o755)
    scenario = tmp_path / "scenario.yaml"
    scenario.write_text("autopilot: primary\n", encoding="utf-8")
    output = tmp_path / "output"
    output.mkdir()

    result = await ExternalSimulationRunner(Path("unavailable"), 5).run(
        scenario_path=scenario,
        output_directory=output,
        log_path=output / "stdout.log",
        executable_path=executable,
    )

    assert result.exit_code == 0
    assert Path(captured.read_text(encoding="utf-8").strip()) == build_dir


@pytest.mark.asyncio
async def test_external_runner_uses_output_parameter_file_without_cli_option(
    tmp_path: Path,
) -> None:
    captured = tmp_path / "arguments.txt"
    executable = tmp_path / "jsb-sim-runner"
    executable.write_text(
        f"#!/bin/sh\nprintf '%s\\n' \"$@\" > '{captured}'\n",
        encoding="utf-8",
    )
    executable.chmod(0o755)
    scenario = tmp_path / "scenario.yaml"
    scenario.write_text("scenario_type: roll_hold\n", encoding="utf-8")
    output = tmp_path / "output"
    output.mkdir()
    parameters = output / "parameters.yaml"
    parameters.write_text("controller_parameters:\n  FW_RR_P: 0.08\n", encoding="utf-8")

    await ExternalSimulationRunner(executable, 5).run(
        scenario_path=scenario,
        output_directory=output,
        log_path=output / "stdout.log",
        parameters_path=parameters,
    )

    assert captured.read_text(encoding="utf-8").splitlines() == [
        "--scenario",
        str(scenario),
        "--output",
        str(output),
    ]
