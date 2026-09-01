from pathlib import Path

import numpy as np
import pytest
from google.protobuf.descriptor_pb2 import FieldDescriptorProto, FileDescriptorProto
from google.protobuf.descriptor_pool import DescriptorPool
from google.protobuf.message_factory import GetMessageClass
from mcap_protobuf.writer import Writer as ProtobufWriter

from app.analysis.downsampling import uniform_downsample
from app.analysis.mcap_reader import McapRunReader
from app.analysis.roll_hold import compute_roll_hold_metrics
from app.analysis.roll_hold_analyzer import RollHoldAnalyzer
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


def test_mcap_reader_keeps_variant_separate_from_canonical_signal(tmp_path: Path) -> None:
    path = tmp_path / "dual.mcap"
    write_sample_mcap(path)
    reader = McapRunReader()

    assert reader.variants(path) == ["baseline", "primary"]
    primary_time, primary = reader.read_aligned(path, ["roll"], variant="primary")
    baseline_time, baseline = reader.read_aligned(path, ["roll"], variant="baseline")

    assert primary_time.tolist() == baseline_time.tolist()
    assert not np.array_equal(primary["roll"], baseline["roll"])


def test_mcap_reader_keeps_historical_unnamespaced_compatibility(tmp_path: Path) -> None:
    path = tmp_path / "historical.mcap"
    write_sample_mcap(path, variants=())
    reader = McapRunReader()

    assert reader.variants(path) == []
    time, series = reader.read_aligned(path, ["roll"], variant="primary")
    assert len(time) == len(series["roll"])


def test_mcap_reader_decodes_jsb0_roll_control_protobuf(tmp_path: Path) -> None:
    descriptor = FileDescriptorProto(
        name="control.proto",
        package="jsb.telemetry.v1",
        syntax="proto3",
    )
    message = descriptor.message_type.add(name="RollControlState")
    fields = [
        ("sim_time_ns", FieldDescriptorProto.TYPE_UINT64),
        ("commanded_roll_rad", FieldDescriptorProto.TYPE_DOUBLE),
        ("commanded_roll_rate_rad_s", FieldDescriptorProto.TYPE_DOUBLE),
        ("roll_error_rad", FieldDescriptorProto.TYPE_DOUBLE),
        ("roll_rate_rad_s", FieldDescriptorProto.TYPE_DOUBLE),
        ("roll_rate_error_rad_s", FieldDescriptorProto.TYPE_DOUBLE),
        ("aileron_command", FieldDescriptorProto.TYPE_DOUBLE),
        ("roll_rad", FieldDescriptorProto.TYPE_DOUBLE),
    ]
    for number, (name, field_type) in enumerate(fields, start=1):
        message.field.add(
            name=name,
            number=number,
            label=FieldDescriptorProto.LABEL_OPTIONAL,
            type=field_type,
        )
    pool = DescriptorPool()
    pool.Add(descriptor)
    message_class = GetMessageClass(
        pool.FindMessageTypeByName("jsb.telemetry.v1.RollControlState")
    )
    path = tmp_path / "protobuf.mcap"
    with path.open("wb") as stream:
        with ProtobufWriter(stream) as writer:
            for index in range(3):
                stamp = index * 10_000_000
                writer.write_message(
                    "/jsb/primary/control/roll",
                    message_class(
                        sim_time_ns=stamp,
                        commanded_roll_rad=0.1 * index,
                        commanded_roll_rate_rad_s=0.2 * index,
                        roll_error_rad=0.02 * index,
                        roll_rad=0.08 * index,
                        roll_rate_rad_s=0.15 * index,
                        roll_rate_error_rad_s=0.05 * index,
                        aileron_command=0.05 * index,
                    ),
                    log_time=stamp,
                    publish_time=stamp,
                )

    reader = McapRunReader()
    assert set(reader.channels(path)) >= {
        "commanded_roll",
        "commanded_roll_rate",
        "roll",
        "roll_rate",
        "roll_error",
        "roll_rate_error",
        "aileron",
    }
    time, series = reader.read_aligned(
        path,
        ["commanded_roll", "roll", "roll_rate", "roll_error", "roll_rate_error", "aileron"],
    )
    assert time.tolist() == [0.0, 0.01, 0.02]
    assert np.allclose(series["commanded_roll"], [0.0, 0.1, 0.2])
    assert np.allclose(series["roll"], [0.0, 0.08, 0.16])
    assert np.allclose(series["roll_error"], [0.0, 0.02, 0.04])
    assert np.allclose(series["roll_rate_error"], [0.0, 0.05, 0.1])
    assert np.allclose(series["aileron"], [0.0, 0.05, 0.1])


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


def test_roll_hold_analyzer_step_response_metrics_and_command_window() -> None:
    time = np.linspace(0, 20, 2001)
    start = 5.0
    elapsed = np.maximum(0.0, time - start)
    command_deg = np.where(time >= start, 10.0, 0.0)
    roll_deg = np.where(time >= start, 10.0 * (1.0 - np.exp(-elapsed)), 0.0)
    roll_rate_deg_s = np.where(time >= start, 10.0 * np.exp(-elapsed), 0.0)
    aileron = np.where(time >= start, 0.2 * np.exp(-elapsed), 0.0)
    definition = {
        "scenario_type": "roll_hold",
        "initial_condition": {"roll_deg": 0},
        "events": [{
            "time_sec": start,
            "command": {"type": "roll_hold", "roll_deg": 10},
        }],
        "acceptance": {
            "settling_band_deg": 0.1,
            "settling_time_limit_sec": 5,
            "overshoot_limit_deg": 1,
            "max_oscillation_cycles": 2,
        },
    }
    result = RollHoldAnalyzer(McapRunReader()).analyze_series(
        definition,
        time,
        {
            "commanded_roll": np.deg2rad(command_deg),
            "commanded_roll_rate": np.deg2rad(np.gradient(command_deg, time)),
            "roll": np.deg2rad(roll_deg),
            "roll_rate": np.deg2rad(roll_rate_deg_s),
            "aileron": aileron,
        },
    )

    assert result.regions["response"].start_sec == 5.0
    assert result.parameters["command_source"] == "scenario_event"
    assert result.parameters["settling_band_source"] == "scenario"
    assert result.markers["command"].time_sec == 5.0
    assert result.markers["command"].value == 10.0
    assert result.markers["peak_roll_rate"].time_sec == pytest.approx(5.0)
    assert result.metrics["rise_time_s"] == pytest.approx(2.20, abs=0.03)
    assert result.markers["rise_10"].label == "Rise 10%"
    assert result.markers["rise_90"].label == "Rise 90%"
    assert result.markers["rise_90"].time_sec - result.markers["rise_10"].time_sec == pytest.approx(
        result.metrics["rise_time_s"]
    )
    assert result.metrics["settling_time_s"] == pytest.approx(4.61, abs=0.03)
    assert result.metrics["overshoot_deg"] == pytest.approx(0.0)
    assert result.metrics["steady_state_error_deg"] < 0.01
    assert result.regions["steady_state_error"].start_sec == pytest.approx(17.0, abs=0.02)
    assert result.markers["steady_state_mean"].time_sec == pytest.approx(18.5, abs=0.02)
    assert result.markers["steady_state_mean"].value == pytest.approx(10.0, abs=0.01)
    assert result.metrics["rms_tracking_error_deg"] > 0
    assert result.metrics["peak_roll_rate_deg_s"] == pytest.approx(10.0)
    assert result.metrics["peak_aileron"] == pytest.approx(0.2)
    assert result.metrics["rms_aileron"] > 0
    assert result.metrics["aileron_saturation_detected"] is None
    assert result.metrics["residual_oscillation_pp_deg"] < 0.01
    assert result.metrics["aileron_saturation_time_s"] is None
    checks = {check.id: check for check in result.checks}
    assert checks["steady_state_error"].target == 0.1
    assert checks["steady_state_error"].target_source == "scenario"
    assert checks["steady_state_error"].start_sec == result.regions["steady_state_error"].start_sec
    assert {check_id: check.status for check_id, check in checks.items()} == {
        "rise_time": "pass",
        "steady_state_error": "pass",
        "settling_time": "pass",
        "overshoot": "pass",
        "rms_tracking_error": "unavailable",
        "oscillation": "pass",
        "residual_oscillation": "pass",
        "dominant_period": "unavailable",
        "peak_aileron": "unavailable",
        "rms_aileron": "unavailable",
        "saturation_time": "unavailable",
    }


def test_roll_hold_analyzer_detects_oscillation_and_saturation_deterministically() -> None:
    time = np.linspace(0, 15, 1501)
    start = 2.0
    elapsed = np.maximum(0.0, time - start)
    active = time >= start
    command_deg = np.where(active, 5.0, 0.0)
    oscillation = np.where(
        active, 2.0 * np.exp(-0.08 * elapsed) * np.sin(2 * np.pi * 0.5 * elapsed), 0.0
    )
    roll_deg = command_deg + oscillation
    aileron = np.where(active, 0.8 * np.sin(2 * np.pi * 0.5 * elapsed), 0.0)
    definition = {
        "scenario_type": "roll_hold",
        "initial_condition": {"roll_deg": 0},
        "command": {"start_sec": start, "roll_deg": 5},
        "acceptance": {
            "settling_band_deg": 0.1,
            "overshoot_limit_deg": 1,
            "max_oscillation_cycles": 2,
        },
        "control_limits": {"aileron_abs_max": 0.75},
    }
    analyzer = RollHoldAnalyzer(McapRunReader())
    inputs = {
        "commanded_roll": np.deg2rad(command_deg),
        "commanded_roll_rate": np.deg2rad(np.gradient(command_deg, time)),
        "roll": np.deg2rad(roll_deg),
        "roll_rate": np.deg2rad(np.gradient(roll_deg, time)),
        "aileron": aileron,
    }
    first = analyzer.analyze_series(definition, time, inputs)
    second = analyzer.analyze_series(definition, time, inputs)

    assert first == second
    assert first.metrics["oscillation_count"] >= 4
    assert first.metrics["dominant_oscillation_frequency_hz"] == pytest.approx(0.5, abs=0.08)
    assert first.metrics["dominant_oscillation_period_s"] == pytest.approx(2.0, abs=0.3)
    assert first.metrics["aileron_saturation_detected"] is True
    assert first.metrics["aileron_saturation_time_s"] > 0
    assert first.metrics["aileron_saturation_fraction"] > 0
    assert first.metrics["residual_oscillation_pp_deg"] > 0
    assert "aileron_saturation" in first.markers
    assert first.intervals["aileron_saturation"]
    assert {check.id: check.status for check in first.checks}["oscillation"] == "fail"
    codes = {item.code for item in first.assessment}
    assert {"excessive_overshoot", "long_settling_oscillation", "aileron_saturation"} <= codes
    oscillation_assessment = next(
        item for item in first.assessment if item.code == "long_settling_oscillation"
    )
    assert "Dominant period" in oscillation_assessment.message


def test_roll_hold_analyzer_missing_signals_returns_partial_result() -> None:
    time = np.linspace(0, 4, 41)
    definition = {
        "scenario_type": "roll_hold",
        "command": {"start_sec": 1, "roll_deg": 5},
    }
    result = RollHoldAnalyzer(McapRunReader()).analyze_series(
        definition,
        time,
        {"aileron": np.linspace(0, 0.4, len(time))},
    )

    assert result.metrics["rise_time_s"] is None
    assert result.metrics["peak_aileron"] == pytest.approx(0.4)
    assert "roll" in result.missing_signals
    assert result.assessment[0].code == "missing_signals"
    statuses = {check.id: check.status for check in result.checks}
    assert statuses["settling_time"] == "unavailable"
    assert statuses["peak_aileron"] == "unavailable"
    assert statuses["saturation_time"] == "unavailable"


def test_roll_hold_analyzer_records_default_thresholds_and_not_settled() -> None:
    time = np.linspace(0, 6, 61)
    active = time >= 1
    result = RollHoldAnalyzer(McapRunReader()).analyze_series(
        {
            "scenario_type": "roll_hold",
            "command": {"start_sec": 1, "roll_deg": 5},
        },
        time,
        {
            "commanded_roll": np.deg2rad(np.where(active, 5.0, 0.0)),
            "roll": np.deg2rad(np.where(active, 2.0, 0.0)),
        },
    )

    assert result.parameters["settling_band_deg"] == 0.1
    assert result.parameters["settling_band_source"] == "default"
    assert result.parameters["steady_state_error_limit_deg"] == 0.1
    assert result.parameters["steady_state_error_limit_source"] == "default"
    assert result.targets["steady_state_error_deg"].value == 0.1
    assert result.targets["steady_state_error_deg"].source == "default"
    assert result.parameters["rise_time_limit_sec"] == 5
    assert result.parameters["rise_time_limit_source"] == "default"
    assert result.parameters["settling_time_limit_sec"] == 10
    assert result.parameters["settling_time_limit_source"] == "default"
    assert result.parameters["overshoot_limit_deg"] == 1
    assert result.parameters["overshoot_limit_source"] == "default"
    assert result.parameters["max_oscillation_cycles"] == 2
    assert result.parameters["max_oscillation_cycles_source"] == "default"
    assert result.parameters["warn_ratio"] == 1.25
    assert result.targets["residual_oscillation_pp_deg"].value == 1
    assert result.targets["residual_oscillation_pp_deg"].source == "default"
    assert {check.id: check.status for check in result.checks}["settling_time"] == "fail"
    assert "not_settled" in {item.code for item in result.assessment}


def test_roll_hold_analyzer_saturation_duration_and_scenario_targets() -> None:
    time = np.arange(5, dtype=float)
    command_deg = np.full(5, 5.0)
    result = RollHoldAnalyzer(McapRunReader()).analyze_series(
        {
            "scenario_type": "roll_hold",
            "command": {"start_sec": 0, "roll_deg": 5},
            "acceptance": {
                "rise_time_limit_sec": 1.5,
                "residual_pp_limit_deg": 0.25,
                "saturation_time_limit_sec": 0.5,
                "aileron_limit": 0.5,
            },
        },
        time,
        {
            "commanded_roll": np.deg2rad(command_deg),
            "roll": np.deg2rad([0, 4.5, 5.2, 4.9, 5.1]),
            "aileron": np.asarray([0.0, 0.8, 0.7, 0.1, 0.0]),
        },
    )

    assert result.metrics["aileron_saturation_time_s"] == pytest.approx(2.0)
    assert result.metrics["aileron_saturation_fraction"] == pytest.approx(0.5)
    assert result.targets["rise_time_s"].value == 1.5
    assert result.targets["rise_time_s"].source == "scenario"
    assert result.targets["residual_oscillation_pp_deg"].value == 0.25
    assert result.targets["residual_oscillation_pp_deg"].source == "scenario"
    assert result.targets["aileron_saturation_time_s"].value == 0.5
    assert result.targets["aileron_saturation_time_s"].source == "scenario"
    statuses = {check.id: check.status for check in result.checks}
    assert statuses["saturation_time"] == "fail"
    assert statuses["peak_aileron"] == "fail"


def test_roll_hold_analyzer_warns_for_small_target_excess() -> None:
    time = np.arange(6, dtype=float)
    result = RollHoldAnalyzer(McapRunReader()).analyze_series(
        {
            "scenario_type": "roll_hold",
            "command": {"start_sec": 0, "roll_deg": 5},
            "acceptance": {"overshoot_limit_deg": 1.0},
        },
        time,
        {
            "commanded_roll": np.deg2rad(np.full(6, 5.0)),
            "roll": np.deg2rad([0, 4, 6.1, 5.2, 5.0, 5.0]),
        },
    )

    overshoot = next(check for check in result.checks if check.id == "overshoot")
    assert overshoot.actual == pytest.approx(1.1)
    assert overshoot.target == 1.0
    assert overshoot.target_source == "scenario"
    assert overshoot.status == "warn"
