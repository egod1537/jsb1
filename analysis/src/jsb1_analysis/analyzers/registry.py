from __future__ import annotations

from collections.abc import Iterable
from typing import Any, Protocol

from jsb1_analysis.telemetry import TelemetryDataset


class UnsupportedAnalyzerError(LookupError):
    pass


class AnalyzerMissingSignalError(ValueError):
    def __init__(
        self, scenario_type: str, variant: str | None, missing: Iterable[str]
    ) -> None:
        self.scenario_type = scenario_type
        self.variant = variant
        self.missing = tuple(sorted(set(missing)))
        suffix = f" for variant {variant!r}" if variant is not None else ""
        super().__init__(
            f"analyzer {scenario_type!r} is missing required logical signals"
            f"{suffix}: {', '.join(self.missing)}"
        )


class DatasetAnalyzer(Protocol):
    scenario_type: str
    required_signals: frozenset[str]

    def analyze_dataset(
        self,
        dataset: TelemetryDataset,
        *,
        variant: str | None = None,
        **context: object,
    ) -> Any: ...


class AnalyzerRegistry:
    """Small scenario-type registry with signal applicability checks."""

    def __init__(self, analyzers: Iterable[DatasetAnalyzer] = ()) -> None:
        self._analyzers: dict[str, DatasetAnalyzer] = {}
        for analyzer in analyzers:
            self.register(analyzer)

    def register(self, analyzer: DatasetAnalyzer) -> None:
        if analyzer.scenario_type in self._analyzers:
            raise ValueError(f"duplicate analyzer: {analyzer.scenario_type}")
        self._analyzers[analyzer.scenario_type] = analyzer

    def require(self, scenario_type: str) -> DatasetAnalyzer:
        try:
            return self._analyzers[scenario_type]
        except KeyError as exc:
            raise UnsupportedAnalyzerError(
                f"no analyzer supports scenario type {scenario_type!r}"
            ) from exc

    def analyze(
        self,
        scenario_type: str,
        dataset: TelemetryDataset,
        *,
        variant: str | None = None,
        **context: object,
    ) -> Any:
        analyzer = self.require(scenario_type)
        available = set(dataset.available_signals(variant))
        missing = analyzer.required_signals - available
        if missing:
            raise AnalyzerMissingSignalError(scenario_type, variant, missing)
        return analyzer.analyze_dataset(
            dataset,
            variant=variant,
            **context,
        )
