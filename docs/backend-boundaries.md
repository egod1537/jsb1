# Backend application boundaries

The backend uses explicit construction instead of a dependency-injection framework. The permitted runtime dependency direction is:

`API routes -> application services -> domain + repository semantics -> infrastructure adapters`

The `domain` package contains state, identifiers, validation results, lifecycle errors, and other values that do not import FastAPI, persistence, process, or filesystem implementations. Application services coordinate use cases. SQLite repositories expose persistence actions such as `reserve`, `mark_running`, `complete_run`, and `fail_run`; callers do not receive database connections or SQL-shaped helpers. Infrastructure owns Git/worktrees, CMake and simulation processes, SFTP, filesystem safety/atomic writes, deployment commands, and network verification.

## I/O ownership audit

| Concern | Boundary owner | Application consumer |
| --- | --- | --- |
| Git clone/fetch/revision/worktree | `infrastructure/git` | `RepositoryManager` |
| CMake subprocess and build directories | `infrastructure/build` | `BuildManager` |
| Simulator subprocess/executable probe | `infrastructure/execution` | `RunExecutionService`, `HealthService` |
| SFTP/Paramiko | `infrastructure/scenario` | `ScenarioSyncService` |
| Run artifacts and safe directory deletion | `infrastructure/filesystem` | artifact, deletion, snapshot, and scenario services |
| SQLite connections and SQL | `repositories` | creation, execution, query, deployment services |
| HTTP parsing/status mapping | `api` | no downstream consumer |

Compatibility re-export modules under `services/runner.py` and `services/scenario_sources/sftp.py` exist only to avoid breaking internal callers during migration. Their process and SFTP implementations live in infrastructure.

## Transaction ownership

- `BuildRepository.reserve` owns one `BEGIN IMMEDIATE` lookup-and-insert transaction. Concurrent callers share an active/completed immutable build or receive exactly one new reservation. Build paths are written before commit.
- `ComparisonRepository.create_with_runs` owns comparison plus child-run creation in one SQLite transaction. A comparison is never visible without all requested child runs.
- `RunRepository.create` owns insertion of one queued run. Scenario snapshot I/O occurs after the row transaction; failure is compensated by an explicit failed lifecycle transition and no work is dispatched. `finalize_preparation` then atomically stores frozen paths and artifact rows; queued workers only discover and claim rows with that readiness marker.
- Lifecycle methods (`mark_running`, `complete_*`, `fail_*`) use compare-and-set status updates in repository-owned transactions. Invalid prior state raises `InvalidTransition` rather than being inferred from a message.
- External Git, process, filesystem, SFTP, and HTTP work is never held inside an open SQLite transaction.

## Composition roots

`container.py` is the API composition root and is split into repository, execution, scenario, and deployment factories. `worker.py` independently constructs only the repositories, adapters, services, and schedulers required by execution workers; it does not import the FastAPI container.

`tests/test_architecture.py` enforces the import edges: domain isolation, no transport/worker or low-level I/O imports in application services, no HTTP dependency in repositories/infrastructure, no low-level I/O in routes, and worker/API composition separation.

The detailed Run/Build claim, execution, failure, and cancellation policy is documented in [run-execution-lifecycle.md](run-execution-lifecycle.md).

SQLite lifecycle, BuildKey, provenance, artifact metadata, and deletion transaction boundaries are documented in [persistence-and-durable-queue.md](persistence-and-durable-queue.md).
