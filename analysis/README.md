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

JSB1 stores one run beneath `data/runs/<six-digit-id>/`:

```text
data/runs/000042/
├── telemetry.mcap
├── run.json
├── metrics.json
└── stdout.log
```

The loader follows the existing backend MCAP contract: numeric JSON scalar,
`{"value": number}`, a JSON object with numeric signal fields, or little-endian
float64 messages. Canonical signals are:

- `commanded_roll` (rad)
- `roll` (rad)
- `commanded_roll_rate` (rad/s)
- `roll_rate` (rad/s)
- `aileron` (rad)

The existing `roll_cmd`, `cmd_roll`, `roll_rate_cmd`, and `cmd_roll_rate` aliases
are normalized. Unequal channel timelines are aligned once, by linear interpolation
onto the first requested/reference channel within their common time range.

## Loading and exploring a run

```python
from jsb1_analysis.io.mcap import load_mcap
from jsb1_analysis.io.run import load_run

run = load_mcap("../data/runs/000042/telemetry.mcap")
# Equivalent and also loads run.json metadata:
run = load_run("../data/runs/000042")

run.available_signals()
roll = run.signal("roll")
segment = run.slice(20.0, 80.0)
frame = segment.to_dataframe()
```

The MCAP file is decoded once into `RunData`; notebooks, plots, and analyzers reuse
the same validated arrays. Missing signals, empty/non-finite data, mismatched lengths,
and non-monotonic time produce explicit exceptions.

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

plot_roll_hold(segment, config=config)
result = RollHoldAnalyzer(config).analyze(segment)
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
MCAP → RunData
       ├─ Notebook
       │  └─ signal inspection, slicing, plots, filter/threshold experiments,
       │     and hypothesis testing
       └─ RollHoldAnalyzer
          └─ deterministic, repeatable, tested metric extraction
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

## Future backend integration

The console script already provides the intended boundary:

```text
JSB1 backend → subprocess/worker → jsb1-analyze-roll-hold → JSON result
```

A later backend change can invoke that command with a validated artifact path and
persist its JSON. The analysis package itself must remain unaware of FastAPI, SQLite,
Docker, and backend service objects.

