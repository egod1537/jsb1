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
      +-- MCAP telemetry processing
```

## Runtime contract

`RuntimeContractReader` is the only component that knows the filesystem layout
of the JSB0 `contract/` tree. `ScenarioValidator` parses YAML and applies a
supplied JSON Schema. Catalog inspection, SFTP sync, validation HTTP APIs, CI,
and run creation all use that validation core. Execution capabilities and
future telemetry catalog metadata enter through the same runtime-contract
boundary.
Controller parameter metadata follows that boundary through
`contract/execution/parameters.json`; `RuntimeControllerParameterService` contains
the one temporary PX4 Roll Hold adapter used while that JSB0 file is absent.

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
  -> validate against that commit
  -> require compare-only headless capabilities from that commit
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
MCAP and computes metrics; execution does not resolve a
branch, choose a build, or validate a mutable Scenario.

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
