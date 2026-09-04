# Deployment, worker, and infrastructure ownership

## Audit result

JSB1 has two intentionally separate deployment surfaces:

- `deploy.sh`, `auto-deploy.sh`, `rollback.sh`, and `undeploy.sh` are the authoritative host deployment interface. They own host bootstrap, remote branch watching, GitHub deployment/commit status reporting, the durable host unit files under `data/branch-deployments`, and safe operator cleanup. `cleanup-deployments.sh` only removes unused JSB1-labelled Docker resources.
- The Python `/api/deployments` workflow is a compatibility controller for existing database-backed deployment records under `data/deployments`. It does not watch branches or report GitHub status. New host deployments must use the shell entrypoint.

The two surfaces therefore do not share state or jointly decide one deployment's lifecycle. Shell code must not write Python deployment rows, and Python code must not write shell unit files or GitHub status.

## Python boundaries

- `DeploymentManager` owns the persisted application lifecycle and rollback ordering.
- `DeploymentPlanner` owns deterministic slug and hostname policy without I/O.
- `DeploymentRuntimeAdapter` owns Compose/Caddy commands, generated configuration, health probes, and host/worktree path validation.
- `DeploymentConfigurationValidator` is the single Python validation boundary for deployment settings and TLS configuration.
- `DeploymentRepository` owns lifecycle transitions and atomic port reservation.
- `GitRepositoryAdapter`, `GitReferencePolicy`, `RepositoryPathResolver`, and `WorktreeManager` own Git process syntax, ref validation, path translation, and immutable worktrees respectively. `RepositoryManager` remains the compatibility use-case facade.

## Worker lifecycle

`app.worker` owns process signal handling and the single-worker advisory lock. `workers.composition` builds the worker-only dependency graph; it does not import the API composition root. `ExecutionWorker` polls durable SQLite rows and dispatches IDs. Build and run services atomically claim those rows before starting work, so a semaphore controls process concurrency but is never the duplicate-work invariant.

On SIGINT/SIGTERM, run tasks are cancelled and joined before build tasks. The build/run execution services translate cancellation into terminal failed state and terminate their owned child process. At startup, `WorkerRecoveryService` marks stale running build, run, and instance rows failed and records the interrupted pipeline stage. Queued rows remain durable and are polled again.

Health checks are read-only: they ping SQLite, probe runner executability, report embedded/external worker mode, and inspect the canonical repository marker without fetching, building, or mutating state.
