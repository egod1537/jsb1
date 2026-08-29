from __future__ import annotations

import json
from pathlib import Path
from typing import Callable

import numpy as np
import pytest
from mcap.writer import CompressionType, Writer

from app.config.settings import Settings
from app.services.runner import RunnerResult


def write_sample_mcap(path: Path, points: int = 101) -> None:
    time = np.linspace(0, 10, points)
    command = np.where(time >= 1, np.deg2rad(5.0), 0.0)
    roll = np.where(time >= 1, command * (1 - np.exp(-(time - 1) / 0.8)), 0.0)
    roll_rate = np.gradient(roll, time)
    commanded_rate = np.gradient(command, time)
    aileron = np.clip((command - roll) * 0.4, -0.2, 0.2)
    values = {
        "commanded_roll": command,
        "roll": roll,
        "commanded_roll_rate": commanded_rate,
        "roll_rate": roll_rate,
        "aileron": aileron,
    }
    with path.open("wb") as stream:
        writer = Writer(stream, compression=CompressionType.NONE)
        writer.start(profile="jsb1-test")
        schema_id = writer.register_schema(
            name="numeric", encoding="jsonschema", data=b'{"type":"number"}'
        )
        channel_ids = {
            name: writer.register_channel(
                topic=name, message_encoding="json", schema_id=schema_id
            )
            for name in values
        }
        base = 1_000_000_000
        for index, stamp in enumerate(time):
            log_time = base + int(stamp * 1_000_000_000)
            for name, samples in values.items():
                writer.add_message(
                    channel_id=channel_ids[name],
                    log_time=log_time,
                    publish_time=log_time,
                    data=json.dumps(float(samples[index])).encode(),
                )
        writer.finish()


class FakeSimulationRunner:
    def __init__(self, *, exit_code: int = 0, create_telemetry: bool = True) -> None:
        self.exit_code = exit_code
        self.create_telemetry = create_telemetry

    async def run(
        self,
        *,
        scenario_path: Path,
        output_directory: Path,
        autopilot: str,
        log_path: Path,
        executable_path: Path | None = None,
        on_started: Callable[[int], None] | None = None,
    ) -> RunnerResult:
        assert scenario_path.is_file()
        assert autopilot in {"baseline", "primary"}
        if on_started is not None:
            on_started(4242)
        log_path.write_text("fake runner output\n", encoding="utf-8")
        if self.exit_code == 0 and self.create_telemetry:
            write_sample_mcap(output_directory / "telemetry.mcap")
        return RunnerResult(exit_code=self.exit_code, wall_time_sec=0.02)


@pytest.fixture
def settings(tmp_path: Path) -> Settings:
    scenario_dir = tmp_path / "scenarios"
    scenario_dir.mkdir()
    (scenario_dir / "roll_hold_5deg.yaml").write_text("name: roll_hold_5deg\n", encoding="utf-8")
    return Settings(
        data_dir=tmp_path / "data",
        database_path=tmp_path / "data" / "jsb1.db",
        scenario_dir=scenario_dir,
        runner_path=tmp_path / "missing-runner",
        max_concurrent_runs=1,
        run_timeout_sec=5,
    )
