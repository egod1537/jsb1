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
    RollHoldAnalyzer,
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
