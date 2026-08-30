from __future__ import annotations

from app.analysis.downsampling import uniform_downsample
from app.analysis.mcap_reader import McapRunReader, canonical_name
from app.domain.models import AvailableSignalsResponse, SignalMetadata, SignalResponse
from app.repositories.runs import RunRepository
from app.services.artifacts import ArtifactService
from app.services.signal_catalog import signal_definition


class TelemetryQueryService:
    """Read canonical telemetry and apply its one API presentation mapping."""

    def __init__(
        self,
        runs: RunRepository,
        reader: McapRunReader,
        artifacts: ArtifactService,
    ) -> None:
        self.runs = runs
        self.reader = reader
        self.artifacts = artifacts

    def signals(
        self,
        run_id: int,
        channels: str,
        *,
        variant: str | None,
        start: float | None,
        end: float | None,
        max_points: int,
    ) -> SignalResponse:
        if start is not None and end is not None and end < start:
            raise ValueError("end must be greater than or equal to start")
        names = [canonical_name(item) for item in channels.split(",") if item.strip()]
        if not names or len(names) > 20:
            raise ValueError("request between 1 and 20 channels")
        row = self.runs.get_artifact_row(run_id, "telemetry")
        telemetry_path = self.artifacts.resolve(row["path"])
        run = self.runs.get(run_id)
        decoded_variants = self.reader.variants(telemetry_path)
        allowed_variants = decoded_variants or run.variants
        selected_variant = variant
        if selected_variant is None:
            selected_variant = (
                "primary" if "primary" in run.variants
                else run.variants[0] if run.variants else None
            )
        if selected_variant is not None and allowed_variants and selected_variant not in allowed_variants:
            raise ValueError(f"variant not available: {selected_variant}")
        time, values = self.reader.read_aligned(
            telemetry_path,
            names,
            variant=selected_variant,
            start=start,
            end=end,
        )
        source_points = len(time)
        time, values = uniform_downsample(time, values, max_points)
        units: dict[str, str] = {}
        for name in list(values):
            definition = signal_definition(name)
            if definition is None:
                units[name] = "raw"
                continue
            values[name] = definition.convert(values[name])
            units[name] = definition.unit
        return SignalResponse(
            time=time.tolist(),
            series={name: value.tolist() for name, value in values.items()},
            units=units,
            source_points=source_points,
            returned_points=len(time),
        )

    def available(self, run_id: int) -> AvailableSignalsResponse:
        row = self.runs.get_artifact_row(run_id, "telemetry")
        telemetry_path = self.artifacts.resolve(row["path"])
        run = self.runs.get(run_id)
        decoded_variants = self.reader.variants(telemetry_path)
        variants = decoded_variants or run.variants or [run.execution_variant]
        availability = {
            variant: self.reader.channels(telemetry_path, variant=variant)
            for variant in variants
        }
        catalog_names = sorted({
            name for names in availability.values() for name in names
        })
        return AvailableSignalsResponse(
            signals=[
                self._metadata(name)
                for name in catalog_names
            ],
            variants=availability,
        )

    @staticmethod
    def _metadata(name: str) -> SignalMetadata:
        definition = signal_definition(name)
        if definition is None:
            return SignalMetadata(name=name, unit="raw")
        return SignalMetadata(
            name=definition.id,
            display_name=definition.display_name,
            symbol=definition.symbol,
            symbol_latex=definition.symbol_latex,
            unit=definition.unit,
            category=definition.category,
            subcategory=definition.subcategory,
        )
