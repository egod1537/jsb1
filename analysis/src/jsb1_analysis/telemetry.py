from __future__ import annotations

from collections.abc import Mapping, Sequence
from dataclasses import dataclass, field
from types import MappingProxyType

import numpy as np
from numpy.typing import ArrayLike, NDArray

from jsb1_analysis.contracts import SignalCatalog, SignalDefinition


class TelemetryDatasetError(ValueError):
    pass


class MissingSignalError(TelemetryDatasetError, KeyError):
    def __init__(self, signal_id: str, variant: str | None) -> None:
        self.signal_id = signal_id
        self.variant = variant
        suffix = f" for variant {variant!r}" if variant is not None else ""
        super().__init__(f"logical signal {signal_id!r} is missing{suffix}")


@dataclass(frozen=True)
class SignalSeries:
    timestamps: ArrayLike
    values: ArrayLike
    unit: str
    definition: SignalDefinition | None = None

    def __post_init__(self) -> None:
        timestamps = _array(self.timestamps, "timestamps")
        values = _array(self.values, "values")
        if timestamps.size != values.size:
            raise TelemetryDatasetError("signal timestamps and values must align")
        if timestamps.size > 1 and np.any(np.diff(timestamps) <= 0):
            raise TelemetryDatasetError("signal timestamps must be strictly increasing")
        object.__setattr__(self, "timestamps", timestamps)
        object.__setattr__(self, "values", values)


@dataclass(frozen=True)
class TelemetryDataset:
    """Immutable logical-signal telemetry, independent of MCAP/protobuf details."""

    series: Mapping[tuple[str | None, str], SignalSeries]
    signal_catalog: SignalCatalog | None = None
    contract_variants: tuple[str, ...] = ()
    metadata: Mapping[str, object] = field(default_factory=dict)

    def __post_init__(self) -> None:
        if not self.series:
            raise TelemetryDatasetError("dataset contains no signals")
        object.__setattr__(self, "series", MappingProxyType(dict(self.series)))
        object.__setattr__(
            self,
            "contract_variants",
            tuple(dict.fromkeys(self.contract_variants)),
        )
        object.__setattr__(self, "metadata", MappingProxyType(dict(self.metadata)))

    def variants(self) -> tuple[str, ...]:
        observed = {variant for variant, _ in self.series if variant is not None}
        ordered = [item for item in self.contract_variants if item in observed]
        ordered.extend(sorted(observed - set(ordered)))
        return tuple(ordered)

    def selected_variant(self, variant: str | None = None) -> str | None:
        """Resolve the requested variant, including legacy unnamespaced data."""

        return self._selected_variant(variant)

    def available_signals(self, variant: str | None = None) -> tuple[str, ...]:
        selected = self._selected_variant(variant)
        return tuple(
            sorted(name for item_variant, name in self.series if item_variant == selected)
        )

    def signal(self, signal_id: str, variant: str | None = None) -> SignalSeries:
        selected = self._selected_variant(variant)
        try:
            return self.series[(selected, signal_id)]
        except KeyError as exc:
            raise MissingSignalError(signal_id, selected) from exc

    def align(
        self,
        signal_ids: Sequence[str],
        *,
        variant: str | None = None,
        start: float | None = None,
        end: float | None = None,
    ) -> tuple[NDArray[np.float64], dict[str, NDArray[np.float64]]]:
        if not signal_ids:
            raise TelemetryDatasetError("at least one logical signal is required")
        selected = self._selected_variant(variant)
        selected_series = [self.signal(item, selected) for item in signal_ids]
        lower = max(float(item.timestamps[0]) for item in selected_series)
        upper = min(float(item.timestamps[-1]) for item in selected_series)
        if start is not None:
            lower = max(lower, start)
        if end is not None:
            upper = min(upper, end)
        reference = selected_series[0].timestamps
        mask = (reference >= lower) & (reference <= upper)
        timeline = reference[mask]
        if timeline.size == 0:
            raise TelemetryDatasetError(
                "requested logical signals do not share a common time range"
            )
        values: dict[str, NDArray[np.float64]] = {}
        for signal_id, item in zip(signal_ids, selected_series, strict=True):
            values[signal_id] = (
                item.values[mask]
                if np.array_equal(item.timestamps, reference)
                else np.interp(timeline, item.timestamps, item.values)
            )
        return timeline, values

    def unit(self, signal_id: str) -> str:
        if self.signal_catalog is None:
            return "raw"
        try:
            return self.signal_catalog.by_logical_id()[signal_id].unit
        except KeyError as exc:
            raise MissingSignalError(signal_id, None) from exc

    def _selected_variant(self, variant: str | None) -> str | None:
        observed = self.variants()
        if variant is not None:
            if not observed and any(key[0] is None for key in self.series):
                return None
            return variant
        return observed[0] if observed else None


def _array(value: ArrayLike, label: str) -> NDArray[np.float64]:
    result = np.asarray(value, dtype=np.float64)
    if result.ndim != 1:
        raise TelemetryDatasetError(f"{label} must be one-dimensional")
    if result.size == 0:
        raise TelemetryDatasetError(f"{label} must not be empty")
    if not np.all(np.isfinite(result)):
        raise TelemetryDatasetError(f"{label} contains non-finite values")
    result = result.copy()
    result.flags.writeable = False
    return result
