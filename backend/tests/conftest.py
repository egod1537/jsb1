from __future__ import annotations

import json
from pathlib import Path
from typing import Callable

import numpy as np
import pytest
import yaml
from mcap.writer import CompressionType, Writer

from app.config.settings import Settings
from app.services.runner import RunnerResult


def write_sample_mcap(
    path: Path,
    points: int = 101,
    variants: tuple[str, ...] = ("baseline", "primary"),
) -> None:
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
        selected_variants: tuple[str | None, ...] = variants or (None,)
        channel_ids = {
            (variant, name): writer.register_channel(
                topic=f"/jsb/{variant}/control/{name}" if variant else name,
                message_encoding="json",
                schema_id=schema_id,
            )
            for variant in selected_variants
            for name in values
        }
        base = 1_000_000_000
        for index, stamp in enumerate(time):
            log_time = base + int(stamp * 1_000_000_000)
            for variant in selected_variants:
                for name, samples in values.items():
                    value = float(samples[index])
                    if variant == "baseline" and name == "roll":
                        value *= 0.9
                    writer.add_message(
                        channel_id=channel_ids[(variant, name)],
                        log_time=log_time,
                        publish_time=log_time,
                        data=json.dumps(value).encode(),
                    )
        writer.finish()


class FakeSimulationRunner:
    def __init__(self, *, exit_code: int = 0, create_telemetry: bool = True) -> None:
        self.exit_code = exit_code
        self.create_telemetry = create_telemetry
        self.variants_used: list[str] = []
        self.parameter_sets_used: list[dict[str, float]] = []

    async def run(
        self,
        *,
        scenario_path: Path,
        output_directory: Path,
        log_path: Path,
        executable_path: Path | None = None,
        parameters_path: Path | None = None,
        on_started: Callable[[int], None] | None = None,
    ) -> RunnerResult:
        assert scenario_path.is_file()
        if parameters_path is not None:
            assert parameters_path.is_file()
            payload = yaml.safe_load(parameters_path.read_text(encoding="utf-8"))
            self.parameter_sets_used.append(payload["controller_parameters"])
        else:
            self.parameter_sets_used.append({})
        self.variants_used.extend(["baseline", "primary"])
        if on_started is not None:
            on_started(4242)
        log_path.write_text("fake runner output\n", encoding="utf-8")
        if self.exit_code == 0 and self.create_telemetry:
            write_sample_mcap(output_directory / "telemetry.mcap")
            (output_directory / "run.json").write_text(json.dumps({
                "mode": "compare",
                "execution": {"variants": ["baseline", "primary"]},
                "results": {
                    "baseline": {"status": "completed"},
                    "primary": {"status": "completed"},
                },
            }), encoding="utf-8")
        return RunnerResult(exit_code=self.exit_code, wall_time_sec=0.02)


@pytest.fixture
def settings(tmp_path: Path) -> Settings:
    scenario_dir = tmp_path / "scenarios"
    scenario_dir.mkdir()
    base_scenario = {
        "schema_version": 1,
        "scenario_type": "roll_hold",
        "name": "C172 Roll Hold 5deg",
        "aircraft": "c172x",
        "autopilot": "primary",
        "controller_parameters": [
            "FW_R_TC",
            "FW_RR_P",
            "FW_RR_I",
            "FW_RR_D",
            "FW_RR_FF",
            "FW_RR_IMAX",
        ],
        "initial_condition": {
            "altitude_ft": 3000,
            "airspeed_kts": 100,
            "roll_deg": 0,
            "pitch_deg": 0,
            "heading_deg": 0,
        },
        "environment": {"wind_enabled": False},
        "trim": {"enabled": True, "mode": "Full"},
        "simulation": {"duration_sec": 30},
        "command": {"start_sec": 5, "roll_deg": 5},
        "acceptance": {
            "settling_band_deg": 0.5,
            "settling_time_limit_sec": 10,
            "overshoot_limit_deg": 1.0,
            "max_oscillation_cycles": 2,
        },
    }
    (scenario_dir / "roll_hold_5deg.yaml").write_text(
        yaml.safe_dump(base_scenario, sort_keys=False), encoding="utf-8"
    )
    (scenario_dir / "roll_hold_5deg_baseline.yaml").write_text(
        yaml.safe_dump(
            {**base_scenario, "name": "C172 Roll Hold 5deg Baseline", "autopilot": "baseline"},
            sort_keys=False,
        ),
        encoding="utf-8",
    )
    return Settings(
        data_dir=tmp_path / "data",
        database_path=tmp_path / "data" / "jsb1.db",
        scenario_dir=scenario_dir,
        runner_path=tmp_path / "missing-runner",
        max_concurrent_runs=1,
        run_timeout_sec=5,
        bootstrap_runtime_repository=False,
    )
