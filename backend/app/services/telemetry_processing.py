from __future__ import annotations

import json
from dataclasses import dataclass
from pathlib import Path

from app.analysis.mcap_reader import McapRunReader
from app.analysis.roll_hold import compute_roll_hold_metrics
from app.domain.models import Metric
from app.domain.telemetry import RuntimeSignalCatalog

REQUIRED_METRIC_CHANNELS = ["commanded_roll", "roll", "aileron"]


@dataclass(frozen=True)
class TelemetryProcessingResult:
    metrics: list[Metric]
    simulation_time_sec: float
    metrics_payload: dict[str, object]


class RunTelemetryProcessor:
    """Consume a telemetry artifact without participating in Run execution."""

    def __init__(self, reader: McapRunReader) -> None:
        self.reader = reader

    def process(
        self,
        telemetry_path: Path,
        existing_metrics_path: Path,
        *,
        variants: list[str] | None = None,
        signal_catalog: RuntimeSignalCatalog | None = None,
        descriptor: bytes | None = None,
    ) -> TelemetryProcessingResult:
        dataset = self.reader.dataset(
            telemetry_path,
            signal_catalog=signal_catalog,
            descriptor=descriptor,
            variants=tuple(variants or ()),
        )
        selected_variants = variants or list(dataset.variants()) or [""]
        metrics_by_variant: dict[str, list[Metric]] = {}
        durations: list[float] = []
        for variant in selected_variants:
            timeline, series = dataset.align(
                REQUIRED_METRIC_CHANNELS,
                variant=variant or None,
            )
            metrics_by_variant[variant] = compute_roll_hold_metrics(
                timeline,
                series["commanded_roll"],
                series["roll"],
                series["aileron"],
            )
            durations.append(float(timeline[-1] - timeline[0]))
        preferred = selected_variants[0]
        metrics = metrics_by_variant[preferred]
        previous = self._read_existing_metrics(existing_metrics_path)
        payload: dict[str, object] = {
            **previous,
            "variants": {
                variant: {
                    "metrics": {
                        metric.name: {"value": metric.value, "unit": metric.unit}
                        for metric in variant_metrics
                    }
                }
                for variant, variant_metrics in metrics_by_variant.items()
            },
            "metrics": {
                metric.name: {"value": metric.value, "unit": metric.unit}
                for metric in metrics
            },
            "definitions": metric_definitions(),
        }
        return TelemetryProcessingResult(
            metrics=metrics,
            simulation_time_sec=max(durations),
            metrics_payload=payload,
        )

    @staticmethod
    def write_metrics(path: Path, payload: dict[str, object]) -> None:
        path.write_text(
            json.dumps(payload, indent=2, allow_nan=False), encoding="utf-8"
        )

    @staticmethod
    def _read_existing_metrics(path: Path) -> dict[str, object]:
        if not path.is_file():
            return {}
        try:
            value = json.loads(path.read_text(encoding="utf-8"))
            return value if isinstance(value, dict) else {}
        except (OSError, json.JSONDecodeError):
            return {}


def metric_definitions() -> dict[str, str]:
    return {
        "settling_time_sec": (
            "Time after the first command change until absolute roll tracking error "
            "remains within ±0.5 deg; null when it never settles."
        ),
        "overshoot_deg": (
            "Largest actual-roll excursion beyond commanded roll in the final command direction."
        ),
        "rms_error_deg": "RMS commanded-minus-actual roll error after command onset.",
        "steady_state_error_deg": (
            "Absolute mean commanded-minus-actual roll error over the last 20% of samples."
        ),
        "max_abs_aileron_deg": "Maximum absolute aileron deflection over the complete run.",
    }
