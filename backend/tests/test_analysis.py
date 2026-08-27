from pathlib import Path

import numpy as np

from app.analysis.downsampling import uniform_downsample
from app.analysis.mcap_reader import McapRunReader
from app.analysis.roll_hold import compute_roll_hold_metrics
from tests.conftest import write_sample_mcap


def test_uniform_downsampling_keeps_endpoints() -> None:
    time = np.arange(100, dtype=float)
    sampled_time, sampled = uniform_downsample(time, {"roll": time * 2}, 10)
    assert len(sampled_time) == 10
    assert sampled_time[[0, -1]].tolist() == [0, 99]
    assert sampled["roll"][-1] == 198


def test_mcap_reader_and_time_window(tmp_path: Path) -> None:
    path = tmp_path / "sample.mcap"
    write_sample_mcap(path)
    reader = McapRunReader()
    assert set(reader.channels(path)) >= {"commanded_roll", "roll", "aileron"}
    time, series = reader.read_aligned(path, ["roll", "aileron"], start=2, end=4)
    assert time[0] >= 2 and time[-1] <= 4
    assert len(time) == len(series["roll"])


def test_roll_hold_metric_definitions() -> None:
    time = np.linspace(0, 10, 101)
    command = np.where(time >= 1, np.deg2rad(5), 0)
    roll = np.where(time >= 1, command * (1 - np.exp(-(time - 1))), 0)
    aileron = (command - roll) * 0.5
    metrics = {item.name: item for item in compute_roll_hold_metrics(time, command, roll, aileron)}
    assert metrics["settling_time_sec"].value is not None
    assert metrics["overshoot_deg"].value == 0
    assert 0 < (metrics["rms_error_deg"].value or 0) < 5
    assert metrics["max_abs_aileron_deg"].unit == "deg"

