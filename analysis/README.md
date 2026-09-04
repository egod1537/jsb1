# JSB1 Python analysis environment

This directory is an independently installable Python package for exploring and
repeatedly analyzing JSB1 run artifacts. It does not import FastAPI, a database
session, or backend services.

## Setup

Install [uv](https://docs.astral.sh/uv/) once, then create the isolated environment:

```sh
cd analysis
uv sync
uv run pytest
uv run jupyter lab
```

`uv sync` installs the runtime package plus the default development group containing
JupyterLab, ipykernel, and pytest. The environment remains beneath `analysis/.venv`.

## Artifact and signal contract

For indexed JSB0 revisions, use `load_run_bundle(run_root, runtime_root)`. The
loader discovers the exact run metadata, scenario, parameter snapshot, telemetry,
signal catalog, variants, and descriptor through `contract/index.json` and
`artifacts.json`; it does not assume artifact filenames.

The MCAP decoder resolves logical signal ids, protobuf topic/field mappings,
units, and variants exclusively from that revision's `signals.yaml`. Unequal
signal timelines are aligned once, by linear interpolation within their common
time range. The returned `TelemetryDataset` keeps wire SI values and exposes
unit metadata from the catalog.

`load_mcap()` and `load_run()` remain compatibility entry points for older,
pre-index artifacts. Their aliases and default filenames are not used by the
indexed contract path.

## Loading and exploring a run

```python
from jsb1_analysis.io import load_run_bundle

bundle = load_run_bundle("../data/runs/000042", "../data/worktrees/jsb0-exact")
dataset = bundle.telemetry_dataset()

dataset.variants()
roll = dataset.signal("roll", variant="primary")
time, signals = dataset.align(
    ["commanded_roll", "roll", "roll_rate", "aileron"],
    variant="primary",
    start=20.0,
    end=80.0,
)
```

The MCAP file is decoded once into `TelemetryDataset`; notebooks, backend
services, plots, and analyzers reuse the same validated arrays. Unsupported
analyzers and missing required logical signals have distinct exceptions.

## Roll-hold analysis

```python
from jsb1_analysis.analyzers.roll_hold import RollHoldAnalyzer, RollHoldConfig
from jsb1_analysis.plotting.timeseries import plot_roll_hold

config = RollHoldConfig(
    command_signal="commanded_roll",
    roll_signal="roll",
    roll_rate_signal="roll_rate",
    aileron_signal="aileron",
)

result = RollHoldAnalyzer(config).analyze(dataset, variant="primary")
result.as_dict()
```

The result contains rise time, settling time, overshoot percentage, steady-state
error, roll-rate dominant frequency, peak absolute roll rate, and peak absolute
aileron. Canonical angle/rate values remain radians and radians/second.

For a subprocess-friendly JSON result:

```sh
uv run jsb1-analyze-roll-hold \
  --mcap ../data/runs/000042/telemetry.mcap \
  --start 20 \
  --end 80
```

## Notebook versus analyzer

```text
exact contract + MCAP → TelemetryDataset
                        ├─ backend analysis/presentation
                        ├─ notebook exploration
                        └─ AnalyzerRegistry
                           └─ RollHoldAnalyzer, future Pitch/Course/TECS analyzers
```

The notebook is intentionally a library consumer. Stable FFT, step-response, and
roll-hold algorithms belong under `src/jsb1_analysis`, not in notebook cells.

Open `notebooks/roll_hold_exploration.ipynb` after starting JupyterLab. Set
`JSB1_MCAP_PATH` before launch to override its default sample path:

```sh
JSB1_MCAP_PATH=../data/runs/000042/telemetry.mcap uv run jupyter lab
```

## Frequency policy

FFT functions require strictly increasing, effectively uniform sample intervals.
They do not silently interpolate irregular telemetry. Explicitly resample in an
exploratory workflow, inspect the result, and only then package an accepted policy
as reusable library code.

## Backend integration

The backend depends directly on this package for decoding, logical datasets,
frequency analysis, and metric primitives. Its reader adds caching and maps
library errors to backend errors; it does not contain a second decoder or metric
copy. The analysis package remains unaware of FastAPI, SQLite, Docker, and
backend service objects.
