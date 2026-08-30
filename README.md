# JSB1

JSB1 is a single-host Simulation Regression & Analysis Server intended to run on a Mac mini. It executes `jsb-sim-runner` headlessly, indexes runs in SQLite, stores large artifacts on the local filesystem, calculates roll-hold metrics, and provides a separate web dashboard. It deliberately does not reproduce the JSB0 interactive GUI or introduce distributed infrastructure.

## Architecture

```text
Caddy (optional, :8080)
├── /          frontend/dist (React)
└── /api/*     FastAPI (:8000)
                  ├── SQLite metadata + durable job queue
                  └── data/runs/<six-digit-id>/ artifacts

Execution worker
└── queued Build/Run → CMake / jsb-sim-runner
```

The backend is layered as follows:

```text
backend/app/
├── api/           thin HTTP routes and dependencies
├── domain/        values, invariants, lifecycle states, and domain errors
├── services/      application orchestration and use cases
├── repositories/  SQLite access and state transitions
├── infrastructure/ Git, worktree, CMake, and deployment verification adapters
├── analysis/      MCAP adapter, downsampling, roll-hold metrics
├── workers/       durable queue consumer and bounded execution schedulers
├── config/        environment-backed settings
└── container.py   explicit dependency composition root
```

The responsibility boundaries and Scenario/Run/Build flows are documented in
[`docs/architecture.md`](docs/architecture.md).

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

Independent MCAP exploration and repeatable Python analyzers live in
[`analysis/`](analysis/README.md):

```sh
cd analysis
uv sync
uv run pytest
uv run jupyter lab
```

Vite serves the UI at `http://localhost:5173` and proxies `/api` to `http://127.0.0.1:8000`. Copy `.env.example` to `.env` at the repository root or export the variables before starting the backend:

| Variable | Default | Purpose |
| --- | --- | --- |
| `JSB1_DATA_DIR` | `./data` (repository data dir) | database and run artifact root |
| `JSB1_DATABASE_PATH` | `<data>/jsb1.db` | SQLite file |
| `JSB_SIM_RUNNER_PATH` | `jsb-sim-runner` | executable path or name on `PATH` |
| `JSB_SCENARIO_DIR` | `./scenarios` | read-only YAML scenario root |
| `JSB1_SCENARIO_SFTP_HOST` | unset | enables the optional remote scenario source |
| `JSB1_SCENARIO_SFTP_PORT` | `22` | SFTP port |
| `JSB1_SCENARIO_SFTP_USER` | unset | SFTP account |
| `JSB1_SCENARIO_SFTP_ROOT` | `/` | recursively scanned remote root |
| `JSB1_SCENARIO_SFTP_KEY_PATH` | unset | preferred private key; mounted read-only in Docker |
| `JSB1_SCENARIO_SFTP_KNOWN_HOSTS_PATH` | OpenSSH default | explicit trusted host-key file |
| `JSB1_SCENARIO_SFTP_PASSWORD` | unset | password fallback; key/agent authentication is preferred |
| `JSB1_MAX_CONCURRENT_RUNS` | `1` | subprocess concurrency bound |
| `JSB1_EXECUTION_MODE` | `embedded` | `embedded` for host development; Compose sets `external` |
| `JSB1_WORKER_POLL_INTERVAL_SEC` | `0.5` | external worker durable-queue polling interval |
| `JSB1_RUN_TIMEOUT_SEC` | `1800` | per-run timeout |
| `JSB1_REPOSITORY_ROOT` | `<data>/repositories` | managed repository root |
| `JSB0_REPOSITORY_URL` | `https://github.com/egod1537/jsb0.git` | canonical JSB0 Runtime remote |
| `JSB0_REPOSITORY_PATH` | `<repository-root>/jsb0` | optional canonical clone path; configured relative values resolve from the project root |
| `JSB0_DEFAULT_BRANCH` | `impl` | preferred initial branch in the New Run form |
| `JSB1_WORKTREE_ROOT` | `<data>/worktrees` | detached commit worktrees |
| `JSB1_DEPLOYMENT_ROOT` | `<data>/deployments` | generated per-revision Compose overrides |
| `JSB1_BUILD_ROOT` | `<data>/builds` | immutable build outputs and logs |
| `JSB1_MAX_CONCURRENT_BUILDS` | `1` | execution-worker build concurrency |
| `JSB1_BUILD_JOBS` | `2` | jobs passed to `cmake --build -j` |
| `JSB1_BUILD_TIMEOUT_SEC` | `3600` | timeout for each CMake command |
| `JSB1_BUILD_EXECUTABLE_RELATIVE_PATH` | `jsb-sim-runner` | fixed executable path beneath each build directory |
| `JSB1_CORS_ORIGINS` | localhost Vite origins | JSON list of allowed development origins |

JSB1 manages exactly one canonical JSB0 Runtime repository. Repository registration
through the web UI is intentionally unsupported. Operators can override the defaults
in the process environment or, for a host-native backend, in
`~/.config/jsb1/jsb1.env`:

```sh
JSB0_REPOSITORY_URL=https://github.com/egod1537/jsb0.git
JSB0_REPOSITORY_PATH=/Users/yang/proj/jsb0
JSB0_DEFAULT_BRANCH=impl
```

On startup JSB1 normalizes the configured remote, reuses a matching historical
repository row when possible, creates or updates the single `jsb0` row, and clones
an absent/empty checkout. It never deletes legacy repository rows or their build/run
foreign keys.

## Docker

Docker Compose builds the React frontend, serves it through Caddy, and proxies API requests to FastAPI. SQLite and run artifacts remain in the host `data/` directory, while scenario YAML files are read from `scenarios/`. `JSB0_REPOSITORY_PATH` is a host path in Compose configuration and is bind-mounted at the fixed container path `/runtime/jsb0`; the backend never interprets a host-only path inside the container.

```sh
docker compose up --build -d
```

Open `http://localhost:8081`. Stop the services with `docker compose down`. To use a different host port, set `JSB1_HTTP_PORT` before starting Compose.

Compose runs API and execution as separate services. The API records queued work
in SQLite; `worker` owns CMake and simulation subprocesses. Restarting only
`backend` or `web` does not interrupt active worker jobs.

Docker Compose does not automatically read `~/.config/jsb1/jsb1.env`; export or
source those values before `docker compose up`, or place non-secret development
values in the ignored project `.env`. `deploy.sh` and its automatic watcher read only
the supported `JSB0_*` and scenario SFTP values from that host file (without overriding exported values),
apply the same host-path-to-`/runtime/jsb0` bind mount, and create the default empty
clone directory before Compose validation.

The simulation runner is not included in this repository. The dashboard and API can run without it, and `/api/health` reports `runner_available: false`. To execute simulations, place an executable at `bin/jsb-sim-runner` and start with the runner override:

```sh
docker compose -f compose.yaml -f compose.runner.yaml up --build -d
```

The runner command is an argv list, never a shell string:

```text
jsb-sim-runner --scenario <validated-path> --output <run-directory>
```

One invocation records baseline and primary into the same `telemetry.mcap`.
The compare-only mode and its variants come from
`contract/execution/capabilities.json` in the resolved immutable JSB0 revision,
not a JSB1 hardcoded list.

## Bundled scenarios and contract validation

The repository `scenarios/` directory contains sample and smoke fixtures, not the
production scenario library:

- `scenarios/samples/` contains canonical examples for people and UI development.
- `scenarios/smoke/` contains the smallest useful contract fixtures for CI.

Operational scenarios come from the validated SFTP source or the local managed
source; the repository directory is intentionally not that catalog. JSB0 `main` is the sole
contract source of truth. The `Scenario Contract` GitHub Actions workflow checks out
`egod1537/jsb0` at `main` every day and on every push, pull request, or manual run,
then validates every bundled `.yaml` and `.yml` file against
`contract/scenario/scenario.schema.json` without copying the schema into JSB1.

Run the same schema-only validation locally with adjacent JSB1 and JSB0 checkouts:

```sh
cd backend
python -m pip install -e '.[test]'
cd ..
python scripts/validate-scenarios.py \
  --scenario-dir scenarios \
  --runtime-root ../jsb0
```

This check does not build JSB0, start JSBSim, generate MCAP, or access a remote
scenario source.

### SFTP remote scenario library

SFTP is optional and manual-sync-only in this iteration. Put operator-owned values
in `~/.config/jsb1/jsb1.env`; never commit a private key or password:

```sh
JSB1_SCENARIO_SFTP_HOST=192.168.0.10
JSB1_SCENARIO_SFTP_PORT=22
JSB1_SCENARIO_SFTP_USER=yang
JSB1_SCENARIO_SFTP_ROOT=/srv/jsb-scenarios
JSB1_SCENARIO_SFTP_KEY_PATH=/Users/yang/.ssh/id_ed25519
JSB1_SCENARIO_SFTP_KNOWN_HOSTS_PATH=/Users/yang/.ssh/known_hosts
```

Keep the private key outside the repository with owner-only permissions
(`chmod 600`). Deployment refuses an in-repository or group/world-accessible key.

Authentication tries the configured private key, then the SSH agent/default key,
then the optional password. Paramiko verifies the server against OpenSSH
`known_hosts` and rejects unknown or mismatched host keys; auto-accept is never
enabled. `deploy.sh` maps configured key/known-host files to fixed read-only
container paths without baking either into an image.

Trigger and inspect sync with:

```sh
curl -X POST https://impl-jsb.mangagaki.net/api/scenarios/sync
curl https://impl-jsb.mangagaki.net/api/scenarios/sync/status
curl https://impl-jsb.mangagaki.net/api/scenarios/invalid
```

The sync recursively reads only `.yaml`/`.yml`, validates all content against the
freshly fetched canonical JSB0 `main` schema, computes SHA-256, then atomically
replaces `data/scenarios/remote/<relative-id>`. SQLite stores catalog metadata only,
never YAML blobs. An invalid update leaves the previous valid cache in service; a
connection outage leaves the backend and all cached scenarios available. A remote
deletion removes the item from the active catalog without deleting historical run
snapshots.

Arbitrary YAML can use the same validation core:

```sh
curl -X POST http://127.0.0.1:8000/api/scenarios/validate \
  -H 'Content-Type: application/json' \
  --data-binary '{"yaml":"schema_version: 1\nscenario_type: roll_hold\nname: Example\naircraft: c172x\n..."}'
```

Compatibility validation (HTTP, bundled CI, SFTP sync) uses JSB0 `main`. Run
creation always validates the selected cached content again against the exact
immutable JSB0 commit chosen for that run. Scenario YAML describes experiment
conditions only; execution variants such as `baseline` and `primary` are selected
separately from the runtime execution contract.

### Scenario Library and immutable inspection

Open `/scenarios` in the web console to browse bundled, managed, and SFTP catalog entries.
`New Scenario` opens the roll-hold builder, whose draft must validate against the
latest JSB0 `main` contract before it can be saved. The backend repeats validation
and publishes the YAML without overwriting existing files. Managed scenarios live
outside the repository at `data/scenarios/managed` by default; set
`JSB1_MANAGED_SCENARIO_DIR` in `~/.config/jsb1/jsb1.env` to use another protected
host-local directory.

The builder's `Roll Hold Test` preset keeps experiment conditions in Scenario YAML
and controller tuning in a separate parameter set. `Save Scenario` persists only
the conditions. `Save & Run` forwards the parameter set to New Run, where it can be
changed again without cloning the Scenario. JSB0 main currently exposes the PX4
fixed-wing metadata in code rather than a machine-readable parameter contract, so
JSB1's single compatibility adapter mirrors `FW_R_TC`, `FW_R_RMAX`, `FW_RR_P`,
`FW_RR_I`, `FW_RR_D`, `FW_RR_FF`, and `FW_RR_IMAX` until
`contract/execution/parameters.json` is available.

The table loads lightweight metadata first; selecting an entry fetches its full
definition and original YAML on demand. The shared read-only inspector exposes
Overview, semantic Definition, Raw YAML, and structured Validation tabs. Invalid
remote revisions are inspectable but remain excluded from the New Run selector.

The inspection endpoints are:

- `POST /api/scenarios` — validate and create a non-overwriting managed scenario
- `GET /api/scenario-catalog` — bundled, managed, and active SFTP catalog, including invalid entries
- `GET /api/scenario-catalog/detail?source=bundled&id=samples/example.yaml`
- `GET /api/runs/{id}/scenario` — frozen execution snapshot with checksum integrity
- `GET /api/comparisons/{id}/scenario` — the comparison's shared frozen snapshot

Run and Comparison pages never substitute the current library file for historical
content. They read the exact `scenario.yaml` snapshot captured at creation, compare
its bytes with the stored SHA-256, and report a visible integrity warning on mismatch.

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

- `GET /api/repositories` (legacy repository metadata)
- `POST /api/repositories` (deprecated compatibility endpoint)
- `GET /api/repositories/{id}` (legacy repository metadata)
- `DELETE /api/repositories/{id}` (deprecated compatibility endpoint)
- `POST /api/repositories/{id}/fetch` (legacy compatibility endpoint)
- `GET /api/repositories/{id}/branches` (legacy compatibility endpoint)
- `GET /api/repositories/{id}/revisions/{revision}` (legacy compatibility endpoint)
- `GET|POST /api/builds`
- `GET /api/builds/{id}`
- `POST /api/builds/{id}/rebuild`
- `GET /api/builds/{id}/logs/{stdout|stderr}`
- `GET|POST /api/deployments`
- `GET /api/deployments/{id}`
- `POST /api/deployments/{id}/redeploy`
- `POST /api/deployments/{id}/restart`
- `DELETE /api/deployments/{id}?force=false`
- `GET /api/health`
- `GET /api/version`
- `GET /api/scenarios`
- `GET /api/scenarios/invalid`
- `POST /api/scenarios/validate`
- `POST /api/scenarios/validate/batch`
- `POST /api/scenarios/sync`
- `GET /api/scenarios/sync/status`
- `GET /api/runtime/repository`
- `POST /api/runtime/repository/fetch`
- `GET /api/runtime/branches`
- `POST /api/runs`
- `GET /api/runs?status=&scenario=&limit=`
- `GET /api/runs/{id}`
- `GET /api/runs/{id}/metrics`
- `GET /api/runs/{id}/signals?variant=&signals=&start=&end=&max_points=`
- `GET /api/runs/{id}/signals/available`
- `GET /api/runs/{id}/analysis/roll-hold`
- `GET /api/runs/{id}/artifacts`
- `GET /api/runs/{id}/artifacts/{kind}`

## Repository and build lineage

JSB1 treats repository identity as platform configuration, not a user-created or
run-level resource. It manages one logical repository key, `jsb0`, backed by the
configured URL and path. A build request resolves the supplied branch or revision to
a full commit SHA, creates or reuses a detached worktree at
`worktrees/<repository-id>/<commit-sha>`, and runs these fixed commands:

```sh
cmake -S <worktree> -B <build-directory> \
  -DJSB_BUILD_EDITOR=OFF -DBUILD_DOCS=OFF
cmake --build <build-directory> -j <configured-jobs> \
  --target jsb-sim-runner
```

The HTTP API never accepts a build command or executable path. Set
`JSB1_BUILD_EXECUTABLE_RELATIVE_PATH` to the JSB0 CMake output path relative to the
build directory. Completed builds are reused by repository and commit; the rebuild
endpoint creates a fresh build record and directory.

JSB1 currently operates against one configured JSB0 Runtime repository. The
preferred `POST /api/runs` contract accepts only the branch instead of a user-selected
repository, build, or commit:

```json
{
  "branch": "impl",
  "scenario": "samples/c172_roll_hold_5deg_baseline.yaml",
  "scenario_source": "bundled"
}
```

At queue time the backend obtains the configured JSB0 Runtime repository internally,
fetches it, and resolves the branch again (preferring `origin/<branch>`). It stores
the repository foreign key, requested branch, and immutable full commit SHA, then
looks up a completed build by repository and commit. A cache hit is reused. On a miss, the build is queued
and the run remains queued until that build finishes, then executes automatically.

New Run inputs are Scenario, JSB0 Branch, and optional tuning parameters. The Branch
is resolved once and the resulting immutable commit derives one reusable build:

```text
Branch ----------------> commit
Commit ----------------> build
Scenario+Build --------> run
Run -------------------> baseline + primary results
```

Scenario YAML is validated against
`contract/scenario/scenario.schema.json` from that exact JSB0 commit before a run is
queued. The resolved compare capability and variant list remain in the Run row as
immutable execution provenance. JSB1 ingests JSB0's `run.json` without overwriting it
and writes its own lineage to `jsb1-run.json`. At queue time JSB1 also atomically snapshots the
exact validated YAML to `data/runs/<id>/scenario.yaml` and stores its scenario ID,
source, and SHA-256, so later remote edits cannot change a historical run.
For parameterized Roll Hold execution, JSB1 stores the full effective controller
parameter set and explicit overrides in SQLite and `jsb1-run.json`. Variant-specific
parameter provenance is retained when the Runtime contract declares it. A non-default set is
also atomically written to `data/runs/<id>/parameters.yaml` and supplied to the
runner as one structured `--parameters <path>` argument; individual gains never
become runner flags or Scenario fields.
Run Detail provides Primary, Baseline, and Overlay views for intra-Run comparison.
Selecting two Run records from the Runs page remains the separate inter-Run
comparison workflow. Deprecated single-variant fields and unnamespaced MCAP artifacts
remain readable for historical Runs but are not used by the New Run UI.

The legacy optional `repository_id`, `build_id`, and `commit_sha` request fields
remain accepted for existing clients and historical data. New UI flows do not expose
them. Run records and `jsb1-run.json` retain repository, requested branch, build, and
commit lineage even if the canonical repository later changes branches or advances
HEAD.

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

## Branch Preview Deployments

The controller can run one isolated Compose project per JSB1 branch. A branch is a
moving user selection; every deployment record resolves it after `git fetch` and
pins the actual runtime to an immutable full commit SHA in a managed worktree:

```text
repository + branch → commit SHA → worktrees/<repository-id>/<commit-sha>
                    → Compose project + isolated data volume
                    → Caddy Host route
```

`main` is special and always maps to:

```text
https://jsb.mangagaki.net
```

Every other branch uses a DNS-safe slug as the leftmost label:

```text
impl          → https://impl-jsb.mangagaki.net
feature/foo   → https://feature-foo-jsb.mangagaki.net
```

Slugs are lowercase, replace runs of non-`[a-z0-9-]` characters with `-`, collapse
and trim hyphens, and truncate the base label to 48 characters. If two different
branch names normalize to the same slug, JSB1 adds the short commit SHA (and a
deterministic branch digest only if the SHA also collides). Hostnames and ports are
always calculated by the server and cannot be supplied through the HTTP API.

### TLS certificate

Set both paths to an existing Cloudflare Origin Certificate and its private key:

```sh
JSB1_TLS_CERT_PATH=/etc/cloudflare/jsb-origin.pem
JSB1_TLS_KEY_PATH=/etc/cloudflare/jsb-origin.key
```

The certificate SAN must cover both of these names (or an equivalent range):

```text
DNS:jsb.mangagaki.net
DNS:*.mangagaki.net
```

JSB1 checks the public certificate SAN before starting a container, checks that
both configured files exist and are readable, rejects a world-readable key, and
refuses certificate or key paths inside this repository. It never reads or logs private-key contents;
generated Caddy fragments contain paths only. Do not use
`~/.cloudflared/cert.pem` as an Origin Certificate when it is an Argo Tunnel token.
If no suitable certificate exists, create one in the Cloudflare dashboard with the
two SANs above and install it outside the checkout; JSB1 does not issue one.

You can inspect only the public certificate metadata safely with:

```sh
openssl x509 -in "$JSB1_TLS_CERT_PATH" -noout -subject -issuer -ext subjectAltName
```

### DNS and Cloudflare Tunnel

The CLI reuses the existing locally-managed `jsb1` Tunnel for every branch. It
generates one shared ingress configuration with `jsb.mangagaki.net` and
`*.mangagaki.net` forwarding to the central Caddy edge. For every deployed branch,
`cloudflared tunnel route dns` creates an exact proxied CNAME to that tunnel. Exact
records avoid taking over the zone's existing wildcard route, which is used by
other services. Repeated deployments reuse the same record, tunnel connector,
Compose project, port reservation, and worktree.

The existing `~/.cloudflared/cert.pem` account credential creates and verifies DNS
routes and also authorizes exact-record deletion on undeploy. An optional
`CLOUDFLARE_API_TOKEN` (Zone Read + DNS Edit) can be used instead for deletion.
No credential contents are copied into this repository.

Browser TLS terminates at Cloudflare Universal SSL and every generated hostname is
exactly one label below `mangagaki.net`. The separate Origin Certificate protects
the Tunnel-to-Caddy hop and is never presented directly to browsers.

### CLI deployment helpers

For this checkout, the simplest production path is the repository-level helper.
It does not check out a branch in the developer working tree. Instead, it fetches
the requested remote branch, resolves an immutable commit, and creates a detached
worktree beneath ignored runtime state:

```text
data/branch-deployments/
├── worktrees/<slug>/<commit>/  detached source worktree
├── units/<slug>/data/          branch-private SQLite and artifacts
├── routes/<slug>.caddy         generated exact-Host Caddy route
└── cloudflared/config.yml      generated shared tunnel ingress (no secret)
```

Deploy, list, and remove instances from the repository root:

```sh
./deploy.sh main
./deploy.sh impl
./deploy.sh 'feature/foo'
./deploy.sh --status
./deploy.sh --remove impl
./undeploy.sh impl
```

Each deploy resolves one immutable full commit SHA before the image build and
records a UTC build timestamp in the unit state. The same branch, commit,
timestamp, and hostname are injected into the backend runtime and baked into the
frontend bundle. Check the local record and the running preview with:

```sh
./deploy.sh --status
curl https://impl-jsb.mangagaki.net/api/version
```

The Navbar shows the baked deployed revision (for example `impl @ 8efb066`). Its
tooltip contains the full SHA, localized build time, and hostname; clicking a
known SHA opens that exact `egod1537/jsb1` commit on GitHub. Development builds
fall back to `dev` / `unknown` without inspecting Git or a remote branch.

### GitHub deployment reporting

GitHub reporting is an optional observability layer over the existing pipeline.
Give the deployment host a fine-grained personal access token or GitHub App token
with these repository permissions, then expose it only to the host process running
the deployment:

- **Deployments: Read and write**
- **Commit statuses: Read and write**
- **Metadata: Read-only** (required automatically by GitHub)

Store persistent host deployment credentials outside the repository in the protected
deployment environment file:

```sh
mkdir -p ~/.config/jsb1
cat > ~/.config/jsb1/deploy.env <<'EOF'
export JSB1_GITHUB_TOKEN='...'
EOF
chmod 600 ~/.config/jsb1/deploy.env

./deploy.sh impl
```

The default path is `~/.config/jsb1/deploy.env`; override it with
`JSB1_DEPLOY_ENV_FILE=/absolute/path/to/deploy.env`. The file is optional, is read
for every manual or automatic deployment process, and may also contain other
host-only deployment settings such as `JSB1_GITHUB_REPOSITORY` and
`JSB1_GITHUB_DEPLOYMENT_REQUIRED`. An already-set process environment value wins
over the value in `deploy.env`, so one-off shell overrides remain possible. The
loader warns if the file is not owned by the current user or has group/world
permissions.

A classic personal access token needs the broader `repo` scope to cover both APIs;
a fine-grained token is preferred.

`GITHUB_TOKEN` is accepted as a fallback. The token is never passed to Compose,
Docker builds, the frontend, or `/api/version`, and is never saved in deployment
unit state. Do not add it to the repository `.env`, `.env.example`, a worktree, or
a cron command. The generated crontab stores only the deployment environment file
path. `auto-deploy.sh --once` loads the protected file at process startup, so token
rotation takes effect on the next scan without reinstalling the crontab.

After `deploy.sh` resolves the immutable SHA, it reports through both APIs:

- **Deployment API** records `jsb1/<branch-slug>` environment lifecycle, deployment
  history, immutable commit, and environment URL.
- **Commit Status API** records `pending`, `success`, or `failure` on that exact SHA
  with context `jsb1/deploy/<branch-slug>` and links to the deployed site.

Commit Status `pending` is attempted first, immediately after the branch or explicit
revision resolves to a full SHA and before GitHub Deployment creation or container
build. Manual deploy, cron/`--once`, rollback, and a recreated branch all enter this
same `deploy.sh` lifecycle. A recreated branch pointing to a new SHA therefore gets
a new status lifecycle; existing statuses on its old SHA remain historical records.

Only successful `/api/version`, Caddy, Cloudflare routing, and public HTTPS
verification cause Deployment and Commit Status `success`. A later commit creates
new records for its SHA; an explicit rollback reports against the selected old SHA.
Successful undeploy reports only the latest Deployment as `inactive`; it does not
rewrite the commit's deploy-attempt result. Deployment objects are never deleted.
After posting Commit Status `success`, the deployer reads GitHub's combined status
for the same full SHA and verifies that the expected context exists in `success`.
Missing or mismatched status is recorded locally as `error` and logged as a warning;
it does not change the successfully deployed service.
For example:

```text
commit a20391f → environment jsb1/impl → https://impl-jsb.mangagaki.net
```

GitHub reporting is non-blocking by default. A missing token, API outage, or rate
limit produces a concise warning while the core deployment continues. Enforce it
only when desired:

```sh
export JSB1_GITHUB_DEPLOYMENT_REQUIRED=true
```

The status table reads both locally recorded GitHub states without making API calls.
It also shows the reported SHA, context, and post-success verification state:

```sh
./deploy.sh --status
```

In GitHub, open **Deployments** or **Environments** for environment history and the
current active deployment. Open a branch, commit list, commit detail, or related pull
request to see the Commit Status indicator; its details link opens the matching
`https://<branch>-jsb.mangagaki.net` site.

### Automatic deployment after push

On the deployment Mac, install the headless-safe per-user crontab watcher once:

```sh
./auto-deploy.sh --install
```

When `~/.config/jsb1/deploy.env` is configured as above, this same command enables
GitHub Deployment and Commit Status reporting for cron-triggered deployments. Check
credential readiness without exposing its value:

```sh
./auto-deploy.sh --status
# Deployment environment: configured
# GitHub reporting credential: configured
```

It checks `origin` every minute. A newly created preview branch, or a new commit
on an existing preview branch, is deployed with the exact remote commit SHA. The core
watcher does not require a GitHub token, webhook, or repository secret; optional
GitHub Deployment reporting uses the host-only token described above. It also detects
branches that do not contain a GitHub Actions workflow file. Repeated scans are
no-ops when the deployed SHA already matches the remote head. A failed SHA is
retried after five minutes; a newer push is attempted immediately.

`main` is excluded from automatic deployment by default. A changed main head is
logged as skipped and remains available for explicit deployment with
`./deploy.sh main`. Opt in and persist the setting only when automatic production
deployment is intentionally enabled:

```sh
JSB1_AUTO_DEPLOY_MAIN=true ./auto-deploy.sh --install
```

```sh
git push origin feature/foo
# within one polling interval:
# https://feature-foo-jsb.mangagaki.net

./auto-deploy.sh --status
./deploy.sh --status
```

The headless-safe watcher is installed in the current user's crontab and writes its
local log beneath the ignored `data/branch-deployments/logs/` directory. Docker
Desktop or OrbStack must be
running for deployment to succeed. When a preview branch disappears from `origin`,
the watcher records `stale-since`. If it stays absent for
`JSB1_STALE_BRANCH_GRACE_SEC` (default 86400, 24 hours), the watcher calls the normal
`undeploy.sh` flow. Restoring the branch during the grace period clears the stale
marker. `main` is never stale-removed. Stopped deployments and retained branch data
are also not deleted automatically. To disable the watcher without touching running
deployments, use `./auto-deploy.sh --uninstall`.

`main` removal requires `./undeploy.sh main --force`. Undeploy removes the
containers, project network, route, worktree, and port reservation, but retains the
branch data directory. A later deploy therefore keeps that branch's SQLite data and
artifacts. The edge and every application container use `restart: unless-stopped`,
so they return when the Docker daemon returns. On macOS, Docker Desktop or OrbStack
must also be configured to start at login.

The helper uses Compose project `jsb1-<slug>` for each branch and `jsb1-edge` for
the shared Caddy proxy and single cloudflared connector. Backend port `8000` remains
private to each project network; only the web container is published on a distinct
`127.0.0.1:8100-8999` port. The
edge publishes HTTP on loopback port 80 and HTTPS on loopback port 4443. Port 4443
avoids the Tailscale listener already using port 443 on this Mac; Cloudflare still
serves the public URL on standard HTTPS port 443. Override it with
`JSB1_EDGE_HTTPS_PORT=443` on a host where port 443 is free.

All long-running application, Caddy, and cloudflared containers use Docker's
`json-file` logger with `max-size: 10m` and `max-file: 5`. The policy is applied when
Compose next creates or recreates each container.

The default external TLS paths are:

```text
/Users/yang/.cloudflare/jsb-origin.pem
/Users/yang/.cloudflare/jsb-origin.key
```

Only these paths are passed to Compose, and the files are read-only bind mounts on
the edge. The helper rejects paths inside the repository, a world-readable key, a
certificate/key mismatch, or a certificate missing either required SAN. No branch
application image receives either file.

The generated Tunnel config points at the Caddy service inside the shared Compose
network. `noTLSVerify` is limited to this encrypted container-to-container origin
hop. Do not run the CLI edge and the legacy
`compose.preview.yaml` edge simultaneously because they are alternative owners of
the same public routes.

Plain deployment always follows the latest `origin/<branch>`. The official explicit
rollback path pins the same hostname to a known-good, locally available commit:

```sh
./deploy.sh main --revision <full-known-good-commit-sha>
./deploy.sh impl --revision <full-commit-sha>
```

After each fully successful public HTTPS deployment, JSB1 records the current and
previous successful commits. If that history is certain, the convenience helper can
select it:

```sh
./rollback.sh impl --dry-run
./rollback.sh impl
```

It refuses to guess from `HEAD^` or ordinary Git history. If no previous successful
marker exists, use the explicit `--revision` command above. Running `./deploy.sh
impl` later moves it back to the current remote branch head.

Build and recreate are separate steps. A build failure therefore leaves existing
containers running. Normal deployment force-recreates `backend` and `web` but
preserves the branch's independent `worker`, so active JSB0 builds and Runs continue.
The first deployment creates the worker. To intentionally roll worker code after
all active work has drained, run:

```sh
JSB1_DEPLOY_RESTART_WORKER=true ./deploy.sh impl
```

An application health failure can still leave new unhealthy API/web containers in
place; the existing Caddy/DNS route is not changed, but this CLI architecture is
not blue-green and does not automatically roll those containers back. Use
`rollback.sh` or explicit `--revision` to restore a recorded good version.

### Operations and recovery helpers

The status command remains state-first and still works when Docker is unavailable.
It now includes container state, the full worktree path, active deployment count,
deployment/worktree disk usage, and a compact Docker disk summary:

```sh
./deploy.sh --status
```

Reboot recovery is read-only and returns non-zero if any global or selected branch
check fails:

```sh
./verify-deployment.sh
./verify-deployment.sh main
./verify-deployment.sh impl
```

It checks Docker, TLS files, edge/cloudflared, Caddy validation, Compose containers,
the loopback `/api/health` endpoint, generated route, public HTTPS, and that the
public `/api/version` branch/commit match unit state without starting or restarting
anything.

Conservative Docker cleanup defaults to a report-only preview:

```sh
./cleanup-deployments.sh --dry-run
./cleanup-deployments.sh
```

Only unused images carrying the JSB1 deployment label or an unambiguous
`jsb1-<branch>-(backend|web)` repository name, plus unattached `jsb1-*` Compose
networks, are eligible. Every image referenced by any running or stopped container
is protected; volumes are never removed. The shared default BuildKit cache is
reported but not pruned because ownership cannot be attributed safely. Cache pruning
is enabled only when `JSB1_DEPLOY_BUILDER` names a dedicated existing JSB1 builder.
The helper never runs `docker system prune -a` or volume pruning.

### Controller startup

The standard `compose.yaml` remains the local single-instance setup. For preview
orchestration, set absolute host paths in a non-committed `.env` and add the preview
override:

```sh
JSB1_HOST_PROJECT_ROOT=/Users/example/proj/jsb1
JSB1_TLS_CERT_PATH=/etc/cloudflare/jsb-origin.pem
JSB1_TLS_KEY_PATH=/etc/cloudflare/jsb-origin.key
JSB1_DEPLOYMENT_PORT_START=8100
JSB1_DEPLOYMENT_PORT_END=8999

docker compose -f compose.yaml -f compose.preview.yaml up --build -d
```

The controller backend receives the Docker socket and the host workspace at the
same absolute path so generated Compose bind paths remain valid. The `edge` Caddy
container uses Docker host networking and owns host ports 80/443. Deployment
application ports bind only to `127.0.0.1`, so Caddy reaches them without exposing
the preview ports on a LAN interface. Enable host networking in Docker Desktop (or
use a runtime such as OrbStack that supports it) before starting this override.
The control UI remains available on `JSB1_HTTP_PORT`
(default `8081`), while public branch hostnames route to managed instances.

Each generated override lives under `data/deployments/<deployment-id>/` and is
ignored by Git. Each project gets its own Compose network, containers, and named
`deployment-data` volume. Caddy routes are independent fragments under
`data/caddy/deployments/`; changes are validated and gracefully loaded with
`caddy reload`, so the edge process is not restarted. On redeploy, the new commit
instance starts and passes HTTP health checks before its Host route replaces the old
instance. The old project is stopped only after the HTTPS route succeeds.

Relevant configuration:

| Variable | Default | Purpose |
| --- | --- | --- |
| `JSB1_TLS_CERT_PATH` | required for deploy | external Origin Certificate path |
| `JSB1_TLS_KEY_PATH` | required for deploy | external private-key path |
| `JSB1_DEPLOYMENT_PORT_START` | `8100` | first allocatable loopback port |
| `JSB1_DEPLOYMENT_PORT_END` | `8999` | last allocatable loopback port |
| `JSB1_DEPLOYMENT_BASE_DOMAIN` | `mangagaki.net` | non-main hostname suffix |
| `JSB1_DEPLOYMENT_MAIN_HOSTNAME` | `jsb.mangagaki.net` | main hostname |
| `JSB1_CLOUDFLARE_TUNNEL` | `jsb1` | shared tunnel name or UUID |
| `JSB1_DEPLOYMENT_HEALTH_TIMEOUT_SEC` | `120` | application/HTTPS health deadline |
| `JSB1_CADDY_CONFIG_PATH` | `<data>/caddy/Caddyfile` | generated root Caddyfile |
| `JSB1_CADDY_FRAGMENTS_DIR` | `<data>/caddy/deployments` | generated Host routes |

The older controller deployment API still accepts a legacy repository ID so existing
deployment records and clients remain valid. This compatibility path is separate
from the canonical JSB0 Runtime and is not a repository-registration workflow in the
Repositories page. New host deployments should use `./deploy.sh <branch>`. The
legacy API response is queued and the UI polls until it becomes `running` or `failed`:

```sh
curl -X POST http://127.0.0.1:8081/api/deployments \
  -H 'Content-Type: application/json' \
  -d '{"repository_id":1,"branch":"impl"}'

curl http://127.0.0.1:8081/api/deployments
curl http://127.0.0.1:8081/api/deployments/3
curl -X POST http://127.0.0.1:8081/api/deployments/3/redeploy
curl -X POST http://127.0.0.1:8081/api/deployments/3/restart
curl -X DELETE http://127.0.0.1:8081/api/deployments/3
```

Stopping `main` is rejected unless the operator explicitly sends
`DELETE /api/deployments/<id>?force=true`. Stop removes the active Caddy fragment,
reloads Caddy, runs Compose down, and releases the ports for reuse. Worktrees remain
under the existing `RepositoryManager` lifecycle so another build or deployment can
reuse the same immutable commit.

## Security and extension seams

Scenario names are resolved beneath the configured scenario root and reject traversal. Execution variants are validated against the selected JSB0 revision; commit fields remain restricted, executable paths and arbitrary runner options are not accepted from HTTP, subprocesses use argv with `shell=False`, artifact paths are resolved beneath the data root, and error paths fail the run without crashing the server.

The replaceable boundaries are `SimulationRunner`, the durable job dispatcher,
the external execution worker, `McapRunReader`, repositories, and pure metric
functions. Future regression suites can add suite/scenario-matrix tables and submit
multiple existing run jobs without changing the Run API.
