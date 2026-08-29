from __future__ import annotations

import numpy as np
import pytest

from jsb1_analysis.metrics.frequency import (
    NonUniformSamplingError,
    dominant_frequency,
    frequency_spectrum,
    spectrum_peaks,
)


def test_dominant_frequency_and_peak_list() -> None:
    time = np.linspace(0.0, 5.0, 2000, endpoint=False)
    signal = 0.7 * np.sin(2.0 * np.pi * 2.0 * time)
    assert dominant_frequency(time, signal) == pytest.approx(2.0, abs=0.01)
    peaks = spectrum_peaks(frequency_spectrum(time, signal), max_peaks=1)
    assert peaks[0].frequency_hz == pytest.approx(2.0, abs=0.01)


def test_constant_signal_has_no_dominant_frequency() -> None:
    time = np.linspace(0.0, 5.0, 1000, endpoint=False)
    assert dominant_frequency(time, np.ones_like(time)) is None


def test_nonuniform_time_requires_explicit_resampling() -> None:
    time = np.linspace(0.0, 5.0, 1000, endpoint=False)
    time[500] += 0.001
    with pytest.raises(NonUniformSamplingError, match="resample explicitly"):
        frequency_spectrum(time, np.sin(time))
