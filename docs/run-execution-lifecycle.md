# Run, build, and execution lifecycle

## Canonical sequence

1. `RunCreationService` loads the selected scenario source.
2. A moving JSB0 branch is fetched and resolved once to an immutable commit SHA.
3. The worktree and contract are loaded from that exact commit. Scenario, capability, variant, and parameter overrides are validated before any execution is queued.
4. Scenario and optional parameter files are frozen beneath the new Run directory. The Run row stores the commit, build, digest, execution mode, variants, and snapshot paths.
5. `BuildManager` reserves or reuses `BuildKey(repository_id, commit_sha)`. A completed cache entry is reusable only when its commit, repository, deterministic paths, completion metadata, and executable all agree.
6. Creation submits IDs only; it never invokes CMake or the simulator.
7. A worker atomically changes one queued Build/Run to running. A duplicate or stale worker receives no claim and performs no process execution.
8. `RunExecutionPlanner` constructs one immutable plan from the stored Run. It never resolves the branch or reloads a mutable scenario source.
9. `SimulationRunner` receives only executable and frozen input/output paths. Runtime duration, timestep, mode, and variant semantics remain in the JSB0 scenario/contract; JSB1 adds no semantic CLI flags.
10. `RunArtifactIngestionService` validates `run.json` and discovers outputs using the exact revision's artifact manifest. It validates immutable commit/scenario provenance before registering artifacts.
11. Telemetry analysis runs only after successful simulation artifact ingestion. The final lifecycle records simulation and analysis outcomes separately in pipeline stages.

## Failure semantics

- A valid `run.json` with `failed` or `interrupted` status is authoritative. Its top-level `error`, then its variant error, is used before the process exit-code diagnostic. Runner stdout and stderr are never parsed for domain status.
- A non-zero exit without a valid structured result is reported as a process exit failure.
- Missing required success artifacts or invalid `run.json` fail artifact ingestion and the Run.
- Telemetry decode or metric analysis failure does not turn a successful simulation into a failed simulation. The Run finishes as `completed`, `collect_artifacts` is marked failed, and `error_message` contains an `analysis failed:` diagnostic.
- Build configure/compile/verification failures remain Build failures and prevent dependent Run claims until the failed Build becomes observable.

## Cancellation and interruption

- Runner timeout sends terminate, waits five seconds, then kills the process if necessary; the Run becomes failed with the typed timeout diagnostic.
- Worker shutdown cancels Run tasks before Build tasks. The runner terminates its subprocess, and running rows are persisted as failed/interrupted work.
- On worker startup, rows left running by a vanished worker are failed explicitly. Queued rows remain the durable queue.
- There is currently no operator cancellation HTTP endpoint. Operators therefore cannot silently mutate a running process through an existing API; adding cancellation requires an explicit cancelling state and process ownership token.
- Retrying a terminal or already-running Run ID is a no-op because the queued-to-running claim is compare-and-set.

## Comparison semantics

JSB1 comparison entities represent multiple independent child Runs and are used only with JSB0 revisions whose headless capability is `single`. Each child Run receives one distinct supported variant while sharing the same commit, build, and scenario digest. A JSB0 `compare` capability remains one Run containing baseline/primary results and is created through the normal Run flow; it is not expanded into duplicate JSB1 child Runs.

## Logs and artifacts

- CMake stdout/stderr use the Build workspace paths.
- Simulator stdout and stderr are separate JSB1 diagnostic artifacts.
- Scenario, parameter set, `run.json`, telemetry, and any additional runtime outputs are resolved from the exact JSB0 artifact manifest.
- `jsb1-run.json` is JSB1 provenance and is stored separately from authoritative JSB0 `run.json`.
