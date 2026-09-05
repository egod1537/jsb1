from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest
from google.protobuf.descriptor_pb2 import (
    FieldDescriptorProto,
    FileDescriptorProto,
    FileDescriptorSet,
)
from google.protobuf.descriptor_pool import DescriptorPool
from google.protobuf.message_factory import GetMessageClass
from jsb1_analysis.analyzers import (
    AnalyzerMissingSignalError,
    AnalyzerRegistry,
    CourseHoldAnalyzer,
    PitchHoldAnalyzer,
    RollHoldAnalyzer,
    TecsAnalyzer,
    UnsupportedAnalyzerError,
)
from jsb1_analysis.contracts import (
    SignalCatalog,
    UnsupportedTelemetryContract,
)
from jsb1_analysis.io.mcap import load_dataset
from jsb1_analysis.metrics import (
    peak_to_peak,
    rms_error,
    saturation_metric,
    settling_time,
    steady_state_error,
)
from mcap_protobuf.writer import Writer as ProtobufWriter


def _descriptor() -> tuple[bytes, type]:
    proto = FileDescriptorProto(
        name="state.proto", package="test.telemetry", syntax="proto3"
    )
    message = proto.message_type.add(name="State")
    for number, name in enumerate(
        ("commanded_roll_rad", "roll_rad", "roll_rate_rad_s", "aileron_command"),
        start=1,
    ):
        message.field.add(
            name=name,
            number=number,
            label=FieldDescriptorProto.LABEL_OPTIONAL,
            type=FieldDescriptorProto.TYPE_DOUBLE,
        )
    pool = DescriptorPool()
    pool.Add(proto)
    message_class = GetMessageClass(pool.FindMessageTypeByName("test.telemetry.State"))
    return FileDescriptorSet(file=[proto]).SerializeToString(), message_class


def _catalog(version: str = "2.1.0") -> SignalCatalog:
    topics = {
        "/jsb/primary/state": {
            "message": "test.telemetry.State",
            "source": "primary",
        },
        "/jsb/baseline/state": {
            "message": "test.telemetry.State",
            "source": "baseline",
        },
    }
    fields = {
        "commanded_roll": ("commanded_roll_rad", "rad"),
        "roll": ("roll_rad", "rad"),
        "roll_rate": ("roll_rate_rad_s", "rad/s"),
        "aileron": ("aileron_command", "normalized"),
    }
    return SignalCatalog.from_mapping(
        {
            "contract_version": version,
            "telemetry_schema_version": 1,
            "topics": topics,
            "signals": {
                f"aircraft.{name}": {
                    "topic": "/jsb/primary/state",
                    "field": field,
                    "type": "float64",
                    "unit": unit,
                    "frame": "body",
                }
                for name, (field, unit) in fields.items()
            },
        }
    )


def _course_descriptor() -> tuple[bytes, type]:
    proto = FileDescriptorProto(
        name="course.proto", package="jsb.telemetry.v1", syntax="proto3"
    )
    message = proto.message_type.add(name="CourseControlState")
    fields = [
        ("commanded_course_rad", FieldDescriptorProto.TYPE_DOUBLE),
        ("course_rad", FieldDescriptorProto.TYPE_DOUBLE),
        ("course_error_rad", FieldDescriptorProto.TYPE_DOUBLE),
        ("ground_speed_m_s", FieldDescriptorProto.TYPE_DOUBLE),
        ("roll_setpoint_rad", FieldDescriptorProto.TYPE_DOUBLE),
        ("roll_rad", FieldDescriptorProto.TYPE_DOUBLE),
        ("roll_limited", FieldDescriptorProto.TYPE_BOOL),
        ("roll_setpoint_rate_limited", FieldDescriptorProto.TYPE_BOOL),
    ]
    for number, (name, field_type) in enumerate(fields, start=1):
        message.field.add(
            name=name,
            number=number,
            label=FieldDescriptorProto.LABEL_OPTIONAL,
            type=field_type,
        )
    pool = DescriptorPool()
    pool.Add(proto)
    message_class = GetMessageClass(
        pool.FindMessageTypeByName("jsb.telemetry.v1.CourseControlState")
    )
    return FileDescriptorSet(file=[proto]).SerializeToString(), message_class


def _course_catalog() -> SignalCatalog:
    topic = "/jsb/baseline/control/course"
    fields = {
        "course.commanded": ("commanded_course_rad", "rad", "local_ned"),
        "course.actual": ("course_rad", "rad", "local_ned"),
        "course.error": ("course_error_rad", "rad", "local_ned"),
        "course.ground_speed": ("ground_speed_m_s", "m/s", "local_ned"),
        "course.roll_setpoint": ("roll_setpoint_rad", "rad", "body"),
        "aircraft.roll": ("roll_rad", "rad", "body"),
        "course.roll_limited": ("roll_limited", "boolean", "controller"),
        "course.roll_setpoint_rate_limited": (
            "roll_setpoint_rate_limited",
            "boolean",
            "controller",
        ),
    }
    return SignalCatalog.from_mapping(
        {
            "contract_version": "2.1.0",
            "telemetry_schema_version": 1,
            "topics": {
                topic: {
                    "message": "jsb.telemetry.v1.CourseControlState",
                    "source": "baseline",
                },
                "/jsb/baseline/control/pitch": {
                    "message": "jsb.telemetry.v1.PitchControlState",
                    "source": "baseline",
                },
            },
            "signals": {
                name: {
                    "topic": topic,
                    "field": field,
                    "type": "bool" if unit == "boolean" else "float64",
                    "unit": unit,
                    "frame": frame,
                }
                for name, (field, unit, frame) in fields.items()
            }
            | {
                "pitch.commanded": {
                    "topic": "/jsb/baseline/control/pitch",
                    "field": "commanded_pitch_rad",
                    "type": "float64",
                    "unit": "rad",
                    "frame": "body",
                },
                "pitch.actual": {
                    "topic": "/jsb/baseline/control/pitch",
                    "field": "pitch_rad",
                    "type": "float64",
                    "unit": "rad",
                    "frame": "body",
                },
                "pitch.error": {
                    "topic": "/jsb/baseline/control/pitch",
                    "field": "pitch_error_rad",
                    "type": "float64",
                    "unit": "rad",
                    "frame": "body",
                },
            },
        }
    )


def _pitch_descriptor() -> tuple[bytes, type]:
    proto = FileDescriptorProto(
        name="pitch.proto", package="jsb.telemetry.v1", syntax="proto3"
    )
    message = proto.message_type.add(name="PitchControlState")
    fields = [
        ("commanded_pitch_rad", FieldDescriptorProto.TYPE_DOUBLE),
        ("pitch_rad", FieldDescriptorProto.TYPE_DOUBLE),
        ("pitch_error_rad", FieldDescriptorProto.TYPE_DOUBLE),
        ("commanded_pitch_rate_rad_s", FieldDescriptorProto.TYPE_DOUBLE),
        ("pitch_rate_rad_s", FieldDescriptorProto.TYPE_DOUBLE),
        ("pitch_rate_error_rad_s", FieldDescriptorProto.TYPE_DOUBLE),
        ("elevator_command", FieldDescriptorProto.TYPE_DOUBLE),
        ("positive_saturation", FieldDescriptorProto.TYPE_BOOL),
        ("negative_saturation", FieldDescriptorProto.TYPE_BOOL),
        ("integrator_limited", FieldDescriptorProto.TYPE_BOOL),
    ]
    for number, (name, field_type) in enumerate(fields, start=1):
        message.field.add(
            name=name,
            number=number,
            label=FieldDescriptorProto.LABEL_OPTIONAL,
            type=field_type,
        )
    pool = DescriptorPool()
    pool.Add(proto)
    message_class = GetMessageClass(
        pool.FindMessageTypeByName("jsb.telemetry.v1.PitchControlState")
    )
    return FileDescriptorSet(file=[proto]).SerializeToString(), message_class


def _pitch_catalog() -> SignalCatalog:
    topic = "/jsb/baseline/control/pitch"
    fields = {
        "pitch.commanded": ("commanded_pitch_rad", "rad"),
        "pitch.actual": ("pitch_rad", "rad"),
        "pitch.error": ("pitch_error_rad", "rad"),
        "pitch_rate.commanded": ("commanded_pitch_rate_rad_s", "rad/s"),
        "pitch_rate.actual": ("pitch_rate_rad_s", "rad/s"),
        "pitch_rate.error": ("pitch_rate_error_rad_s", "rad/s"),
        "actuator.elevator": ("elevator_command", "normalized"),
        "pitch.saturation_positive": ("positive_saturation", "boolean"),
        "pitch.saturation_negative": ("negative_saturation", "boolean"),
        "pitch.integrator_limited": ("integrator_limited", "boolean"),
    }
    return SignalCatalog.from_mapping(
        {
            "contract_version": "2.1.0",
            "telemetry_schema_version": 1,
            "topics": {
                topic: {
                    "message": "jsb.telemetry.v1.PitchControlState",
                    "source": "baseline",
                },
                "/jsb/baseline/control/course": {
                    "message": "jsb.telemetry.v1.CourseControlState",
                    "source": "baseline",
                },
            },
            "signals": {
                name: {
                    "topic": topic,
                    "field": field,
                    "type": "bool" if unit == "boolean" else "float64",
                    "unit": unit,
                    "frame": "controller" if unit == "boolean" else "body",
                }
                for name, (field, unit) in fields.items()
            }
            | {
                "course.commanded": {
                    "topic": "/jsb/baseline/control/course",
                    "field": "commanded_course_rad",
                    "type": "float64",
                    "unit": "rad",
                    "frame": "local_ned",
                },
                "course.actual": {
                    "topic": "/jsb/baseline/control/course",
                    "field": "course_rad",
                    "type": "float64",
                    "unit": "rad",
                    "frame": "local_ned",
                },
                "course.error": {
                    "topic": "/jsb/baseline/control/course",
                    "field": "course_error_rad",
                    "type": "float64",
                    "unit": "rad",
                    "frame": "local_ned",
                },
            },
        }
    )


def _tecs_descriptor() -> tuple[bytes, type]:
    proto = FileDescriptorProto(
        name="tecs.proto", package="jsb.telemetry.v1", syntax="proto3"
    )
    message = proto.message_type.add(name="TecsState")
    fields = [
        ("sim_time_ns", FieldDescriptorProto.TYPE_UINT64),
        ("altitude_agl_m", FieldDescriptorProto.TYPE_DOUBLE),
        ("target_altitude_agl_m", FieldDescriptorProto.TYPE_DOUBLE),
        ("internal_altitude_setpoint_agl_m", FieldDescriptorProto.TYPE_DOUBLE),
        ("airspeed_cas_m_s", FieldDescriptorProto.TYPE_DOUBLE),
        ("target_airspeed_cas_m_s", FieldDescriptorProto.TYPE_DOUBLE),
        ("vertical_speed_m_s", FieldDescriptorProto.TYPE_DOUBLE),
        ("target_vertical_speed_m_s", FieldDescriptorProto.TYPE_DOUBLE),
        ("target_pitch_rad", FieldDescriptorProto.TYPE_DOUBLE),
        ("target_throttle", FieldDescriptorProto.TYPE_DOUBLE),
        ("total_energy_error", FieldDescriptorProto.TYPE_DOUBLE),
        ("energy_balance_error", FieldDescriptorProto.TYPE_DOUBLE),
        ("underspeed_protection_active", FieldDescriptorProto.TYPE_BOOL),
        ("overspeed_protection_active", FieldDescriptorProto.TYPE_BOOL),
        ("throttle_upper_saturated", FieldDescriptorProto.TYPE_BOOL),
    ]
    for number, (name, field_type) in enumerate(fields, start=1):
        message.field.add(
            name=name,
            number=number,
            label=FieldDescriptorProto.LABEL_OPTIONAL,
            type=field_type,
        )
    pool = DescriptorPool()
    pool.Add(proto)
    message_class = GetMessageClass(
        pool.FindMessageTypeByName("jsb.telemetry.v1.TecsState")
    )
    return FileDescriptorSet(file=[proto]).SerializeToString(), message_class


def _tecs_catalog() -> SignalCatalog:
    topic = "/jsb/baseline/control/tecs"
    fields = {
        "tecs.altitude.target": ("target_altitude_agl_m", "m"),
        "tecs.altitude.internal_target": ("internal_altitude_setpoint_agl_m", "m"),
        "tecs.altitude.actual": ("altitude_agl_m", "m"),
        "tecs.airspeed.target": ("target_airspeed_cas_m_s", "m/s"),
        "tecs.airspeed.actual": ("airspeed_cas_m_s", "m/s"),
        "tecs.vertical_speed.target": ("target_vertical_speed_m_s", "m/s"),
        "tecs.vertical_speed.actual": ("vertical_speed_m_s", "m/s"),
        "tecs.pitch_target": ("target_pitch_rad", "rad"),
        "tecs.throttle_target": ("target_throttle", "normalized"),
        "tecs.total_energy_error": ("total_energy_error", "m^2/s^2"),
        "tecs.energy_balance_error": ("energy_balance_error", "m^2/s^2"),
        "tecs.underspeed_active": ("underspeed_protection_active", "boolean"),
        "tecs.overspeed_active": ("overspeed_protection_active", "boolean"),
        "tecs.throttle_upper_saturated": ("throttle_upper_saturated", "boolean"),
    }
    return SignalCatalog.from_mapping({
        "contract_version": "2.1.0",
        "telemetry_schema_version": 1,
        "topics": {topic: {
            "message": "jsb.telemetry.v1.TecsState",
            "source": "baseline",
        }},
        "signals": {
            name: {
                "topic": topic,
                "field": field,
                "type": "bool" if unit == "boolean" else "float64",
                "unit": unit,
                "frame": "controller",
            }
            for name, (field, unit) in fields.items()
        },
    })


def test_exact_descriptor_decodes_logical_signals_and_contract_variants(
    tmp_path: Path,
) -> None:
    descriptor, message_class = _descriptor()
    path = tmp_path / "telemetry.mcap"
    with path.open("wb") as stream, ProtobufWriter(stream) as writer:
        for index in range(3):
            for variant, offset in (("primary", 0.0), ("baseline", 0.5)):
                writer.write_message(
                    f"/jsb/{variant}/state",
                    message_class(
                        commanded_roll_rad=0.2,
                        roll_rad=0.1 * index + offset,
                        roll_rate_rad_s=0.01 * index,
                        aileron_command=0.03 * index,
                    ),
                    log_time=index * 10_000_000,
                    publish_time=index * 10_000_000,
                )

    dataset = load_dataset(
        path,
        signal_catalog=_catalog(),
        descriptor=descriptor,
        variants=("baseline", "primary"),
    )

    assert dataset.variants() == ("baseline", "primary")
    assert dataset.unit("roll") == "rad"
    assert dataset.signal("roll", "baseline").values.tolist() == [0.5, 0.6, 0.7]
    assert dataset.signal("roll", "primary").definition.field == "roll_rad"


def test_contract_version_and_analyzer_applicability_errors_are_distinct(
    tmp_path: Path,
) -> None:
    with pytest.raises(UnsupportedTelemetryContract):
        _catalog("9.0.0")

    descriptor, message_class = _descriptor()
    path = tmp_path / "missing.mcap"
    with path.open("wb") as stream, ProtobufWriter(stream) as writer:
        for index in range(3):
            writer.write_message(
                "/jsb/primary/state",
                message_class(commanded_roll_rad=0.2, roll_rad=0.1 * index),
                log_time=index * 10_000_000,
                publish_time=index * 10_000_000,
            )
    complete_catalog = _catalog()
    incomplete_catalog = SignalCatalog(
        complete_catalog.contract_version,
        complete_catalog.telemetry_schema_version,
        complete_catalog.topics,
        tuple(
            item
            for item in complete_catalog.signals
            if item.logical_id in {"commanded_roll", "roll"}
        ),
    )
    dataset = load_dataset(
        path,
        signal_catalog=incomplete_catalog,
        descriptor=descriptor,
    )
    registry = AnalyzerRegistry((RollHoldAnalyzer(),))
    with pytest.raises(UnsupportedAnalyzerError, match="pitch_hold"):
        registry.analyze("pitch_hold", dataset)
    with pytest.raises(AnalyzerMissingSignalError, match="aileron, roll_rate"):
        registry.analyze("roll_hold", dataset)


def test_course_control_state_decodes_with_exact_descriptor(tmp_path: Path) -> None:
    descriptor, message_class = _course_descriptor()
    path = tmp_path / "course.mcap"
    with path.open("wb") as stream, ProtobufWriter(stream) as writer:
        for index, actual_deg in enumerate((359.0, 1.0, 2.0)):
            writer.write_message(
                "/jsb/baseline/control/course",
                message_class(
                    commanded_course_rad=np.deg2rad(1.0),
                    course_rad=np.deg2rad(actual_deg),
                    course_error_rad=np.deg2rad((2.0, 0.0, -1.0)[index]),
                    ground_speed_m_s=50.0,
                    roll_setpoint_rad=np.deg2rad((5.0, 1.0, 0.0)[index]),
                    roll_rad=np.deg2rad((2.0, 1.0, 0.0)[index]),
                    roll_limited=index == 0,
                    roll_setpoint_rate_limited=index == 0,
                ),
                log_time=index * 1_000_000_000,
                publish_time=index * 1_000_000_000,
            )
    dataset = load_dataset(
        path,
        signal_catalog=_course_catalog(),
        descriptor=descriptor,
        variants=("baseline",),
    )
    assert dataset.variants() == ("baseline",)
    assert np.rad2deg(dataset.signal("course.actual", "baseline").values).tolist() == pytest.approx(
        [359.0, 1.0, 2.0]
    )
    result = AnalyzerRegistry((CourseHoldAnalyzer(),)).analyze(
        "course_hold", dataset, variant="baseline"
    )
    assert result.rms_course_error_rad == pytest.approx(np.deg2rad(np.sqrt(5 / 3)))
    assert result.roll_limit_duration_s == 1.0


def test_pitch_control_state_decodes_with_exact_descriptor(tmp_path: Path) -> None:
    descriptor, message_class = _pitch_descriptor()
    path = tmp_path / "pitch.mcap"
    with path.open("wb") as stream, ProtobufWriter(stream) as writer:
        for index, pitch_deg in enumerate((0.0, 3.0, 5.2, 5.0, 5.0)):
            writer.write_message(
                "/jsb/baseline/control/pitch",
                message_class(
                    commanded_pitch_rad=np.deg2rad(5.0),
                    pitch_rad=np.deg2rad(pitch_deg),
                    pitch_error_rad=np.deg2rad(5.0 - pitch_deg),
                    commanded_pitch_rate_rad_s=np.deg2rad(5.0 - pitch_deg),
                    pitch_rate_rad_s=np.deg2rad((0.0, 3.0, 1.0, 0.05, 0.0)[index]),
                    pitch_rate_error_rad_s=0.0,
                    elevator_command=(0.0, 0.6, -0.1, 0.0, 0.0)[index],
                    positive_saturation=index == 1,
                    negative_saturation=False,
                    integrator_limited=False,
                ),
                log_time=index * 1_000_000_000,
                publish_time=index * 1_000_000_000,
            )
    dataset = load_dataset(
        path,
        signal_catalog=_pitch_catalog(),
        descriptor=descriptor,
        variants=("baseline",),
    )
    assert dataset.available_signals("baseline") == (
        "elevator",
        "integrator_limited",
        "pitch.actual",
        "pitch.commanded",
        "pitch.error",
        "pitch_rate.actual",
        "pitch_rate.commanded",
        "pitch_rate.error",
        "saturation_negative",
        "saturation_positive",
    )
    result = AnalyzerRegistry((PitchHoldAnalyzer(),)).analyze(
        "pitch_hold", dataset, variant="baseline"
    )
    assert np.rad2deg(result.overshoot_rad) == pytest.approx(0.2)
    assert result.max_abs_elevator == 0.6
    assert result.elevator_saturation_duration_s == 1.0


def test_tecs_state_decodes_with_exact_descriptor_and_full_logical_ids(
    tmp_path: Path,
) -> None:
    descriptor, message_class = _tecs_descriptor()
    path = tmp_path / "tecs.mcap"
    with path.open("wb") as stream, ProtobufWriter(stream) as writer:
        for index, altitude in enumerate((100.0, 125.0, 150.0)):
            writer.write_message(
                "/jsb/baseline/control/tecs",
                message_class(
                    sim_time_ns=index * 1_000_000_000,
                    altitude_agl_m=altitude,
                    target_altitude_agl_m=150.0,
                    internal_altitude_setpoint_agl_m=altitude,
                    airspeed_cas_m_s=40.0 + index,
                    target_airspeed_cas_m_s=42.0,
                    vertical_speed_m_s=2.0 if index < 2 else 0.0,
                    target_vertical_speed_m_s=2.0 if index < 2 else 0.0,
                    target_pitch_rad=0.05 if index < 2 else 0.0,
                    target_throttle=0.6 if index < 2 else 0.5,
                    total_energy_error=50.0 - 25.0 * index,
                    energy_balance_error=20.0 - 10.0 * index,
                    underspeed_protection_active=index == 0,
                    overspeed_protection_active=False,
                    throttle_upper_saturated=index == 0,
                ),
                log_time=index * 1_000_000_000,
                publish_time=index * 1_000_000_000,
            )
    dataset = load_dataset(
        path,
        signal_catalog=_tecs_catalog(),
        descriptor=descriptor,
        variants=("baseline",),
    )
    assert "tecs.altitude.target" in dataset.available_signals("baseline")
    assert "tecs.energy_balance_error" in dataset.available_signals("baseline")
    result = AnalyzerRegistry((TecsAnalyzer(),)).analyze(
        "tecs", dataset, variant="baseline"
    )
    assert result.steady_state_mean_altitude_error_m == pytest.approx(25.0)
    assert result.minimum_airspeed_mps == pytest.approx(40.0)
    assert result.underspeed_protection_activated is True
    assert result.throttle_saturation_duration_s == pytest.approx(1.0)


def test_metric_primitives_are_unit_agnostic_and_deterministic() -> None:
    time = np.arange(5, dtype=float)
    reference = np.ones(5)
    response = np.array([0.0, 0.8, 1.1, 1.0, 1.0])
    assert rms_error(reference, response) == pytest.approx(np.sqrt(1.05 / 5))
    assert steady_state_error(reference, response, fraction=0.4) == 0.0
    assert settling_time(time, reference - response, band=0.01) == 3.0
    assert peak_to_peak(response) == 1.1
    saturation = saturation_metric(time, [0.0, 1.0, 1.0, 0.0, 0.0], limit=0.9)
    assert saturation.duration == 2.0
    assert saturation.ratio == 0.5
