# JSB1

JSB1 is a single-host Simulation Regression & Analysis Server intended to run on a Mac mini. It executes `jsb-sim-runner` headlessly, indexes runs in SQLite, stores large artifacts on the local filesystem, calculates roll-hold metrics, and provides a separate web dashboard. It deliberately does not reproduce the JSB0 interactive GUI or introduce distributed infrastructure.

## Architecture

```text
Caddy (optional, :8080)
├── /          frontend/dist (React)
└── /api/*     FastAPI (:8000)
                  ├── SQLite metadata index
                  ├── data/runs/<six-digit-id>/ artifacts
                  └── in-process queue → jsb-sim-runner
```

The backend is layered as follows:

```text
backend/app/
├── api/           thin HTTP routes and dependencies
├── domain/        typed Run, Metric, and Artifact models
├── services/      scenario validation, execution, runner, artifact safety
├── repositories/  SQLite access and state transitions
├── analysis/      MCAP adapter, downsampling, roll-hold metrics
├── workers/       bounded in-process scheduler
└── config/        environment-backed settings
```

## Local development

Python 3.11 or newer and a recent Node.js LTS release are required.

Backend (macOS/Linux):

```sh
cd backend
python3 -m venv .venv
source .venv/bin/activate
python -m pip install -e '.[test]'
uvicorn app.main:app --reload
```

Frontend, in a second terminal:

```sh
cd frontend
npm install
npm run dev
```

Vite serves the UI at `http://localhost:5173` and proxies `/api` to `http://127.0.0.1:8000`. Copy `.env.example` to `.env` at the repository root or export the variables before starting the backend:

| Variable | Default | Purpose |
| --- | --- | --- |
| `JSB1_DATA_DIR` | `./data` (repository data dir) | database and run artifact root |
| `JSB1_DATABASE_PATH` | `<data>/jsb1.db` | SQLite file |
| `JSB_SIM_RUNNER_PATH` | `jsb-sim-runner` | executable path or name on `PATH` |
| `JSB_SCENARIO_DIR` | `./scenarios` | read-only YAML scenario root |
| `JSB1_MAX_CONCURRENT_RUNS` | `1` | subprocess concurrency bound |
| `JSB1_RUN_TIMEOUT_SEC` | `1800` | per-run timeout |
| `JSB1_AUTOPILOTS` | `["primary"]` | JSON whitelist accepted by the API |
| `JSB1_CORS_ORIGINS` | localhost Vite origins | JSON list of allowed development origins |

The runner command is an argv list, never a shell string:

```text
jsb-sim-runner --scenario <validated-path> --output <run-directory> --autopilot <validated-id>
```

If the current runner does not accept `--autopilot`, adapt only `ExternalSimulationRunner` in `backend/app/services/runner.py`; the API, queue, and tests stay unchanged.

## MCAP telemetry contract

The v0 adapter accepts numeric messages in either of these forms:

- one MCAP topic per signal, message encoding `json`, with a JSON number or `{ "value": number }`;
- a JSON object message containing several numeric signal fields;
- one little-endian float64 per topic with message encoding `float64` or `application/x-float64`.

Required semantic signals are `commanded_roll`, `roll`, and `aileron`; plots also use `commanded_roll_rate` and `roll_rate`. The aliases `roll_cmd`, `cmd_roll`, `roll_rate_cmd`, and `cmd_roll_rate` are normalized. MCAP log timestamps define relative simulation time. Unequal channel timelines are linearly aligned to the first requested channel. The adapter is the only MCAP-specific layer and keeps a small mtime/size-keyed decoded cache.

MCAP stores canonical radians and radians/second. `/signals` converts angular values once at the backend boundary and includes a `units` map; the frontend never converts them again.

## Metrics

Metrics are pure NumPy calculations over aligned arrays:

| Metric | Definition |
| --- | --- |
| Settling time | Time after first command change until absolute tracking error stays inside ±0.5 deg; `null` if it never settles |
| Overshoot | Largest actual-roll excursion beyond commanded roll in the final command direction |
| RMS error | RMS commanded-minus-actual roll error after command onset |
| Steady-state error | Absolute mean tracking error over the last 20% of samples |
| Max absolute aileron | Maximum absolute aileron deflection over the entire run |

If the runner has already written an `acceptance` object in `metrics.json`, JSB1 preserves it. JSB1 v0 does not invent pass/fail thresholds.

## Storage and states

SQLite migrations live in `backend/migrations`. The schema has `runs`, `metrics`, and `artifacts`; binaries never enter SQLite. Valid execution transitions are `queued → running → completed|failed`. A queued run remains queued while waiting for the semaphore.

```text
data/runs/000001/
├── telemetry.mcap
├── metrics.json
├── run.json
└── stdout.log
```

The database stores paths relative to `JSB1_DATA_DIR`. Artifact responses expose a filename and controlled download URL, not filesystem paths.

## API

- `GET /api/health`
- `GET /api/scenarios`
- `GET /api/autopilots`
- `POST /api/runs`
- `GET /api/runs?status=&scenario=&limit=`
- `GET /api/runs/{id}`
- `GET /api/runs/{id}/metrics`
- `GET /api/runs/{id}/signals?channels=&start=&end=&max_points=`
- `GET /api/runs/{id}/artifacts`
- `GET /api/runs/{id}/artifacts/{kind}`

Signal responses are capped at 20,000 points and 20 channels. v0 uses uniform index downsampling and never changes the MCAP source.

## Tests

Backend tests use a fake asynchronous runner and generate a small real MCAP fixture with all five channels:

```sh
cd backend
pytest
```

Frontend tests cover list/detail rendering and loading/error states:

```sh
cd frontend
npm test
npm run build
```

## Mac mini deployment

Build the frontend with `npm run build`, run FastAPI as a dedicated launchd service bound to loopback, and install Caddy. From the repository root, `caddy run --config Caddyfile.example` serves the static SPA and proxies `/api`. Adjust the site address and working directory for the machine. Keep the data directory on local persistent storage and back up both `jsb1.db` and `data/runs` together.

For an internal LAN, bind Caddy to the LAN interface and keep FastAPI on `127.0.0.1`. Production CORS should be same-origin (no CORS required) or use an explicit origin; wildcard CORS is not configured.

## Security and extension seams

Scenario names are resolved beneath the configured scenario root and reject traversal. Autopilot and commit fields are restricted, executable paths and arbitrary runner options are not accepted from HTTP, subprocesses use argv with `shell=False`, artifact paths are resolved beneath the data root, and error paths fail the run without crashing the server.

The replaceable boundaries are `SimulationRunner`, `InProcessRunScheduler`, `McapRunReader`, repositories, and pure metric functions. Future regression suites can add suite/scenario-matrix tables and submit multiple existing run jobs through the scheduler. Baselines, scheduled suites, build/checkout, external workers, trend analysis, and notifications can be added at those boundaries without changing v0's Run API.
