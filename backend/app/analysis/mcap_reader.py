from __future__ import annotations

import hashlib
from collections import OrderedDict
from pathlib import Path

import numpy as np
from jsb1_analysis.io.mcap import McapLoadError, load_dataset
from jsb1_analysis.io.run import canonical_signal_name
from jsb1_analysis.telemetry import (
    MissingSignalError,
    TelemetryDataset,
    TelemetryDatasetError,
)
from numpy.typing import NDArray

from app.domain.telemetry import RuntimeSignalCatalog


class McapReadError(RuntimeError):
    pass


def canonical_name(name: str) -> str:
    """Compatibility name normalization for pre-contract API callers."""

    return canonical_signal_name(name)


class McapRunReader:
    """Backend adapter over the shared contract-aware telemetry decoder.

    The reusable ``jsb1_analysis`` package owns MCAP/protobuf decoding and the
    immutable logical-signal dataset. This adapter only adds a small file and
    exact-contract keyed cache plus backend-compatible errors.
    """

    def __init__(self, cache_entries: int = 4) -> None:
        self.cache_entries = cache_entries
        self._cache: OrderedDict[
            tuple[str, int, int, str], TelemetryDataset
        ] = OrderedDict()

    def dataset(
        self,
        path: Path,
        *,
        signal_catalog: RuntimeSignalCatalog | None = None,
        descriptor: bytes | None = None,
        variants: tuple[str, ...] = (),
    ) -> TelemetryDataset:
        if not path.is_file():
            raise McapReadError(f"MCAP file not found: {path.name}")
        stat = path.stat()
        identity = hashlib.sha256(descriptor or b"").hexdigest()
        if signal_catalog is not None:
            identity += ":" + hashlib.sha256(
                repr(signal_catalog).encode("utf-8")
            ).hexdigest()
        identity += ":" + hashlib.sha256(repr(variants).encode("utf-8")).hexdigest()
        key = (str(path.resolve()), stat.st_mtime_ns, stat.st_size, identity)
        if key in self._cache:
            self._cache.move_to_end(key)
            return self._cache[key]
        try:
            result = load_dataset(
                path,
                signal_catalog=signal_catalog,
                descriptor=descriptor,
                variants=variants,
            )
        except McapLoadError as exc:
            raise McapReadError(str(exc)) from exc
        self._cache[key] = result
        while len(self._cache) > self.cache_entries:
            self._cache.popitem(last=False)
        return result

    def variants(
        self,
        path: Path,
        *,
        signal_catalog: RuntimeSignalCatalog | None = None,
        descriptor: bytes | None = None,
        variants: tuple[str, ...] = (),
    ) -> list[str]:
        return list(
            self.dataset(
                path,
                signal_catalog=signal_catalog,
                descriptor=descriptor,
                variants=variants,
            ).variants()
        )

    def channels(
        self,
        path: Path,
        variant: str | None = None,
        *,
        signal_catalog: RuntimeSignalCatalog | None = None,
        descriptor: bytes | None = None,
        variants: tuple[str, ...] = (),
    ) -> list[str]:
        dataset = self.dataset(
            path,
            signal_catalog=signal_catalog,
            descriptor=descriptor,
            variants=variants,
        )
        return list(dataset.available_signals(variant))

    def read_aligned(
        self,
        path: Path,
        channels: list[str],
        *,
        variant: str | None = None,
        start: float | None = None,
        end: float | None = None,
        signal_catalog: RuntimeSignalCatalog | None = None,
        descriptor: bytes | None = None,
        variants: tuple[str, ...] = (),
    ) -> tuple[NDArray[np.float64], dict[str, NDArray[np.float64]]]:
        dataset = self.dataset(
            path,
            signal_catalog=signal_catalog,
            descriptor=descriptor,
            variants=variants,
        )
        requested = [canonical_name(item) for item in channels]
        try:
            return dataset.align(
                requested,
                variant=variant,
                start=start,
                end=end,
            )
        except MissingSignalError as exc:
            missing = [
                item
                for item in requested
                if item not in dataset.available_signals(variant)
            ]
            raise McapReadError(f"channels not found: {', '.join(missing)}") from exc
        except TelemetryDatasetError as exc:
            raise McapReadError(str(exc)) from exc
