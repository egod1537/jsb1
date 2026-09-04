# JSB1 Platform architecture

JSB1 is an engineering platform around one configured JSB0 Runtime. The code
keeps transport and external-system details outside the domain while retaining
the existing REST, SQLite, filesystem, deployment, and runner contracts.

```text
FastAPI / React
      |
      v
Application services (create, execute, sync, deploy)
      |
      v
Domain values and invariants
      |
      v
Persistence repositories and infrastructure adapters
      |
      +-- Git / immutable worktrees
      +-- CMake / JSB0 runner
      +-- SFTP / filesystem
      +-- HTTP / TLS deployment verification
      +-- shared jsb1_analysis MCAP/metric core
```

## Runtime contract

`RuntimeContractReader` is the only component that knows the filesystem layout
of the JSB0 `contract/` tree. `ScenarioValidator` parses YAML and applies a
supplied JSON Schema. Catalog inspection, SFTP sync, validation HTTP APIs, CI,
and run creation all use that validation core. Execution capabilities and
telemetry catalog metadata enter through the same runtime-contract boundary.
Indexed revisions are discovered through `contract/index.json`, materialized as
one immutable-revision `RuntimeContractBundle`, and cached by repository id plus
commit SHA. Controller parameter metadata and validation come from that bundle;
the PX4 Roll Hold adapter is restricted to pre-index legacy revisions.

## Scenario lifecycle

```text
BundledScenarioSource / SftpScenarioSource
  -> ScenarioService catalog and load
  -> ScenarioValidator against a selected Runtime contract
  -> ScenarioSnapshotService atomic immutable copy
```

A catalog Scenario is a mutable source asset. A Run or Comparison snapshot is
immutable provenance below `data/runs/` or `data/comparisons/`; subsequent
source changes cannot alter an existing execution.

## Run creation

`RunCreationService` owns the application transaction/orchestration:

```text
resolve scenario
  -> resolve requested branch to immutable commit
  -> prepare immutable worktree
  -> load the complete Runtime contract from that commit
  -> validate scenario and parameter whitelist against that contract
  -> resolve declared execution mode, variants, and artifact paths
  -> resolve and validate controller parameters for all declared variants
  -> resolve/reuse BuildKey(repository_id, commit_sha)
  -> freeze scenario snapshot
  -> freeze a structured parameter snapshot when overrides exist
  -> persist Run provenance
  -> leave queued ids in the durable SQLite work queue
```

`ComparisonCreationService` applies the same boundary once for its child Runs,
so every child shares the exact scenario bytes, commit, and build.

## Run execution

`RunExecutionService` receives an existing immutable Run id. It asks
`BuildManager` for a verified executable, invokes the injected `SimulationRunner`,
ingests the Runtime-owned `run.json`, registers artifacts, and updates lifecycle
state. `RunTelemetryProcessor` consumes both variant namespaces from the resulting
MCAP through the shared contract-aware `TelemetryDataset` and computes metrics;
execution does not resolve a
branch, choose a build, or validate a mutable Scenario.

## Analysis boundary

The backend and notebook environment share the installable `jsb1_analysis`
package. It owns contract-driven MCAP/protobuf decode, logical signal datasets,
generic metric/frequency primitives, immutable offline Run bundles, and the
analyzer registry. Backend analysis adds cache/error adapters and API-oriented
assessment DTOs only. Indexed telemetry is decoded with the exact Run commit's
signal catalog, exported descriptor, and variant list; display-unit conversion
is deferred to the presentation/query layer.

## Repository and build boundaries

`RepositoryManager` retains canonical JSB0 lifecycle policy and the legacy API
facade. `GitRepositoryAdapter` owns Git process invocation; `WorktreeManager`
owns serialized immutable worktree materialization. `BuildKey` centralizes the
cache identity. `BuildManager` owns persistence/lifecycle and delegates CMake
commands to `CmakeBuildAdapter`.

## Deployment and workers

`DeploymentManager` orchestrates persisted deployment state, Compose/Caddy
runtime changes, and rollback cleanup. `DeploymentVerifier` owns port and
HTTP/TLS probes. GitHub Deployment and Commit Status reporting remains owned by
the existing shell deployment path, avoiding duplicated Python behavior.

The API owns creation and inspection, but it does not own a deployed Build or
Run subprocess. Queued rows in SQLite are the durable queue. `app.worker`
polls those rows, applies the existing build/run semaphore limits, and invokes
`BuildManager` and `RunExecutionService`. API restarts therefore preserve both
queued work and an already-running worker process.

The deployment Compose project keeps `backend`, `web`, and `worker` as separate
services. Normal JSB1 deployments force-recreate only `backend` and `web`; an
existing worker is preserved. Worker code is restarted only on first install or
when the operator explicitly sets `JSB1_DEPLOY_RESTART_WORKER=true` after active
work has drained. Embedded scheduling remains available for host-native
development and tests with `JSB1_EXECUTION_MODE=embedded`.

Build reservation is serialized with a SQLite `BEGIN IMMEDIATE` transaction.
The cache identity is `BuildKey(repository_id, commit_sha)`: a queued or running
build is shared, a valid completed build is reused, and only a missing/invalid
entry creates another build. This prevents concurrent Single/Comparison requests
from compiling the same immutable revision twice.

`app/container.py` is the API composition root; `main.py` only creates FastAPI,
installs middleware/routes, and manages lifespan. `app/worker.py` is the separate
execution composition root.

## Frontend boundaries

Pages remain route composition. Scenario viewer/library state and Run creation
live under `features/scenarios` and `features/runs`; the complete reusable plot
workspace, presets, data sources, and timeline state live under
`features/plots`. A variant-aware Run data source expands the same canonical
plot preset into Primary, Baseline, or de-duplicated Overlay series; selecting
two Runs uses the separate inter-Run data source. Shared Blueprint primitives remain in `components`, HTTP
transport remains in `api`, and response models remain in `types`.
