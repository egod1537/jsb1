from __future__ import annotations

from app.analysis.downsampling import uniform_downsample
from app.analysis.mcap_reader import McapRunReader, canonical_name
from app.domain.models import AvailableSignalsResponse, SignalMetadata, SignalResponse
from app.repositories.runs import RunRepository
from app.services.artifacts import ArtifactService
from app.services.repository_manager import RepositoryManager
from app.services.runtime_contract import RuntimeContractReader, RuntimeSignalCatalog
from app.services.signal_catalog import contract_signal_definition, signal_definition


class TelemetryQueryService:
    """Read canonical telemetry and apply its one API presentation mapping."""

    def __init__(
        self,
        runs: RunRepository,
        reader: McapRunReader,
        artifacts: ArtifactService,
        repositories: RepositoryManager | None = None,
        contract_reader: RuntimeContractReader | None = None,
    ) -> None:
        self.runs = runs
        self.reader = reader
        self.artifacts = artifacts
        self.repositories = repositories
        self.contract_reader = contract_reader or RuntimeContractReader()

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
        catalog, descriptor, contract_variants = self._contract_for_run(run)
        dataset = self.reader.dataset(
            telemetry_path,
            signal_catalog=catalog,
            descriptor=descriptor,
            variants=contract_variants,
        )
        allowed_variants = list(dataset.variants()) or run.variants
        selected_variant = variant
        if selected_variant is None:
            selected_variant = allowed_variants[0] if allowed_variants else None
        if selected_variant is not None and allowed_variants and selected_variant not in allowed_variants:
            raise ValueError(f"variant not available: {selected_variant}")
        time, values = dataset.align(
            names,
            variant=selected_variant,
            start=start,
            end=end,
        )
        source_points = len(time)
        time, values = uniform_downsample(time, values, max_points)
        units: dict[str, str] = {}
        for name in list(values):
            contract_item = catalog.by_api_id().get(name) if catalog else None
            definition = (
                contract_signal_definition(contract_item)
                if contract_item is not None
                else signal_definition(name)
            )
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
        catalog, descriptor, contract_variants = self._contract_for_run(run)
        dataset = self.reader.dataset(
            telemetry_path,
            signal_catalog=catalog,
            descriptor=descriptor,
            variants=contract_variants,
        )
        variants = list(dataset.variants()) or run.variants or [run.execution_variant]
        availability = {
            variant: list(dataset.available_signals(variant))
            for variant in variants
        }
        catalog_names = sorted({
            name for names in availability.values() for name in names
        })
        return AvailableSignalsResponse(
            signals=[
                self._metadata(name, catalog)
                for name in catalog_names
            ],
            variants=availability,
        )

    @staticmethod
    def _metadata(
        name: str, catalog: RuntimeSignalCatalog | None = None
    ) -> SignalMetadata:
        contract_item = catalog.by_api_id().get(name) if catalog else None
        definition = (
            contract_signal_definition(contract_item)
            if contract_item is not None
            else signal_definition(name)
        )
        if definition is None:
            return SignalMetadata(name=name, unit="raw")
        metadata = SignalMetadata(
            name=definition.id,
            display_name=definition.display_name,
            symbol=definition.symbol,
            symbol_latex=definition.symbol_latex,
            unit=definition.unit,
            category=definition.category,
            subcategory=definition.subcategory,
        )
        if contract_item is None:
            return metadata
        return metadata.model_copy(update={
            "contract_id": contract_item.id,
            "topic": contract_item.topic,
            "field": contract_item.field,
            "source_unit": contract_item.unit,
            "frame": contract_item.frame,
            "axis": contract_item.axis,
            "sign": contract_item.sign,
            "group": contract_item.group,
            "description": contract_item.description,
            "range": list(contract_item.value_range) if contract_item.value_range else None,
        })

    def _contract_for_run(
        self, run
    ) -> tuple[RuntimeSignalCatalog | None, bytes | None, tuple[str, ...]]:
        if (
            self.repositories is None
            or run.repository_id is None
            or run.commit_sha is None
            or run.branch is None
        ):
            return None, None, ()
        worktree = self.repositories.prepare_worktree(
            run.repository_id, run.commit_sha
        )
        if not self.contract_reader.is_indexed(worktree):
            return None, None, ()
        bundle = self.contract_reader.load_bundle(
            worktree,
            repository_id=run.repository_id,
            commit_sha=run.commit_sha,
        )
        return bundle.signal_catalog, bundle.telemetry_descriptor, bundle.variants
