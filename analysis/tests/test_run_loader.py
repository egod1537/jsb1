from __future__ import annotations

import json
from pathlib import Path

import numpy as np
import pytest
from mcap.writer import CompressionType, Writer

from jsb1_analysis.cli import main as analysis_cli
from jsb1_analysis.io.mcap import McapLoadError, load_mcap
from jsb1_analysis.io.run import RunData, RunDataError, SignalNotFoundError, load_run


def write_test_mcap(path: Path, points: int = 101) -> None:
    time = np.linspace(0.0, 10.0, points)
    command = np.where(time >= 1.0, np.deg2rad(5.0), 0.0)
    roll = np.where(time >= 1.0, command * (1.0 - np.exp(-(time - 1.0))), 0.0)
    values = {
        "roll_cmd": command,
        "roll": roll,
        "roll_rate": np.gradient(roll, time),
        "aileron": np.clip((command - roll) * 0.4, -0.2, 0.2),
    }
    with path.open("wb") as stream:
        writer = Writer(stream, compression=CompressionType.NONE)
        writer.start(profile="jsb1-analysis-test")
        schema_id = writer.register_schema(
            name="numeric", encoding="jsonschema", data=b'{"type":"number"}'
        )
        channels = {
            name: writer.register_channel(
                topic=name, message_encoding="json", schema_id=schema_id
            )
            for name in values
        }
        base_time = 1_000_000_000
        for index, timestamp in enumerate(time):
            log_time = base_time + int(timestamp * 1_000_000_000)
            for name, samples in values.items():
                writer.add_message(
                    channel_id=channels[name],
                    log_time=log_time,
                    publish_time=log_time,
                    data=json.dumps(float(samples[index])).encode("utf-8"),
                )
        writer.finish()


def test_load_mcap_uses_canonical_signals_and_one_timeline(tmp_path: Path) -> None:
    path = tmp_path / "telemetry.mcap"
    write_test_mcap(path)
    run = load_mcap(path)
    assert run.available_signals() == (
        "aileron",
        "commanded_roll",
        "roll",
        "roll_rate",
    )
    assert run.time[0] == 0.0
    assert run.time[-1] == 10.0
    assert np.array_equal(run.signal("roll_cmd"), run.signal("commanded_roll"))
    assert run.metadata["reference_signal"] == "commanded_roll"


def test_load_run_directory_includes_manifest_metadata(tmp_path: Path) -> None:
    run_directory = tmp_path / "runs" / "000042"
    run_directory.mkdir(parents=True)
    write_test_mcap(run_directory / "telemetry.mcap")
    (run_directory / "run.json").write_text(
        json.dumps({"id": 42, "scenario_name": "roll_hold_5deg"}), encoding="utf-8"
    )
    run = load_run(run_directory)
    assert run.metadata["run"] == {"id": 42, "scenario_name": "roll_hold_5deg"}


def test_run_slice_and_missing_signal_are_explicit() -> None:
    run = RunData(
        time=np.arange(5, dtype=float),
        signals={"roll": np.arange(5, dtype=float)},
    )
    segment = run.slice(1.0, 3.0)
    assert segment.time.tolist() == [1.0, 2.0, 3.0]
    assert segment.signal("roll").tolist() == [1.0, 2.0, 3.0]
    with pytest.raises(SignalNotFoundError, match="aileron"):
        run.signal("aileron")


@pytest.mark.parametrize(
    ("time", "signal", "message"),
    [
        ([], [], "time array is empty"),
        ([0.0, 1.0], [1.0], "expected 2"),
        ([0.0, 0.0], [1.0, 2.0], "strictly increasing"),
        ([0.0, 1.0], [1.0, np.nan], "NaN or infinite"),
    ],
)
def test_run_data_validation(
    time: list[float], signal: list[float], message: str
) -> None:
    with pytest.raises(RunDataError, match=message):
        RunData(time=time, signals={"roll": signal})


def test_missing_mcap_signal_is_explicit(tmp_path: Path) -> None:
    path = tmp_path / "telemetry.mcap"
    write_test_mcap(path)
    with pytest.raises(McapLoadError, match="signals not found: commanded_roll_rate"):
        load_mcap(path, ["commanded_roll_rate"])


def test_cli_returns_subprocess_friendly_json(
    tmp_path: Path, capsys: pytest.CaptureFixture[str]
) -> None:
    path = tmp_path / "telemetry.mcap"
    write_test_mcap(path, points=1001)
    assert analysis_cli(["--mcap", str(path)]) == 0
    result = json.loads(capsys.readouterr().out)
    assert set(result) == {
        "rise_time",
        "settling_time",
        "overshoot",
        "steady_state_error",
        "dominant_frequency_hz",
        "peak_roll_rate",
        "peak_aileron",
    }
