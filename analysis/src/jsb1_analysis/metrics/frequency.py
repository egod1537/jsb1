from __future__ import annotations

from dataclasses import dataclass

import numpy as np
from numpy.typing import ArrayLike, NDArray
from scipy.signal import find_peaks


class NonUniformSamplingError(ValueError):
    """FFT input is not uniformly sampled and must be resampled explicitly."""


@dataclass(frozen=True)
class FrequencySpectrum:
    frequency_hz: NDArray[np.float64]
    amplitude: NDArray[np.float64]
    power: NDArray[np.float64]


@dataclass(frozen=True)
class FrequencyPeak:
    frequency_hz: float
    amplitude: float
    power: float


def _validated_samples(
    time: ArrayLike, signal: ArrayLike, sampling_rtol: float
) -> tuple[NDArray[np.float64], NDArray[np.float64], float]:
    timeline = np.asarray(time, dtype=np.float64)
    values = np.asarray(signal, dtype=np.float64)
    if timeline.ndim != 1 or values.ndim != 1:
        raise ValueError("time and signal must be one-dimensional")
    if timeline.size != values.size:
        raise ValueError("time and signal must have equal lengths")
    if timeline.size < 4:
        raise ValueError("at least four samples are required for frequency analysis")
    if not np.all(np.isfinite(timeline)) or not np.all(np.isfinite(values)):
        raise ValueError("time and signal must contain only finite values")
    intervals = np.diff(timeline)
    if np.any(intervals <= 0):
        raise ValueError("time must be strictly increasing")
    sample_interval = float(np.median(intervals))
    relative_error = float(
        np.max(np.abs(intervals - sample_interval)) / sample_interval
    )
    if relative_error > sampling_rtol:
        raise NonUniformSamplingError(
            "time is not uniformly sampled "
            f"(max relative interval error {relative_error:.3g}); resample explicitly"
        )
    return timeline, values, sample_interval


def frequency_spectrum(
    time: ArrayLike,
    signal: ArrayLike,
    *,
    remove_dc: bool = True,
    window: str | None = "hann",
    sampling_rtol: float = 1e-3,
) -> FrequencySpectrum:
    """Return one-sided FFT amplitude and power without implicit resampling."""

    _timeline, values, sample_interval = _validated_samples(time, signal, sampling_rtol)
    centered = values - np.mean(values) if remove_dc else values.copy()
    if window == "hann":
        weights = np.hanning(values.size)
    elif window is None:
        weights = np.ones(values.size)
    else:
        raise ValueError("window must be 'hann' or None")
    coherent_gain = float(np.mean(weights))
    transformed = np.fft.rfft(centered * weights)
    frequency = np.fft.rfftfreq(values.size, d=sample_interval)
    amplitude = np.abs(transformed) / (values.size * coherent_gain)
    if amplitude.size > 1:
        amplitude[1:-1] *= 2.0
    power = np.square(amplitude)
    if remove_dc:
        frequency = frequency[1:]
        amplitude = amplitude[1:]
        power = power[1:]
    return FrequencySpectrum(
        frequency_hz=frequency.astype(np.float64, copy=False),
        amplitude=amplitude.astype(np.float64, copy=False),
        power=power.astype(np.float64, copy=False),
    )


def dominant_frequency(
    time: ArrayLike,
    signal: ArrayLike,
    *,
    min_frequency: float | None = None,
    max_frequency: float | None = None,
    sampling_rtol: float = 1e-3,
) -> float | None:
    spectrum = frequency_spectrum(time, signal, sampling_rtol=sampling_rtol)
    mask = np.ones(spectrum.frequency_hz.size, dtype=bool)
    if min_frequency is not None:
        mask &= spectrum.frequency_hz >= min_frequency
    if max_frequency is not None:
        mask &= spectrum.frequency_hz <= max_frequency
    if not np.any(mask):
        return None
    candidates = np.flatnonzero(mask)
    peak = candidates[int(np.argmax(spectrum.power[mask]))]
    if spectrum.power[peak] <= np.finfo(np.float64).eps:
        return None
    return float(spectrum.frequency_hz[peak])


def spectrum_peaks(
    spectrum: FrequencySpectrum, *, max_peaks: int | None = None
) -> tuple[FrequencyPeak, ...]:
    """Return local spectrum peaks ordered by descending power."""

    indexes, _properties = find_peaks(spectrum.power)
    ordered = sorted(indexes, key=lambda index: spectrum.power[index], reverse=True)
    if max_peaks is not None:
        if max_peaks < 1:
            raise ValueError("max_peaks must be positive")
        ordered = ordered[:max_peaks]
    return tuple(
        FrequencyPeak(
            frequency_hz=float(spectrum.frequency_hz[index]),
            amplitude=float(spectrum.amplitude[index]),
            power=float(spectrum.power[index]),
        )
        for index in ordered
    )
