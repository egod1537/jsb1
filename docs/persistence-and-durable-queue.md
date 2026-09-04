# Persistence and durable queue

## Ownership audit

| Data | Identity and lifecycle | Ownership and constraints |
| --- | --- | --- |
| `repositories` | Repository id; mutable configuration | Referenced restrictively by Builds, Runs, Comparisons, and Deployments. |
| `builds` | Build attempt id; queued → running → completed/failed | `build_keys` owns the unique `(repository_id, commit_sha)` mapping to the canonical attempt. A partial unique index prevents two active attempts for one key. |
| `runs` | Run id; queued → running → completed/failed | The row is the durable queue item. `output_directory IS NOT NULL` is its readiness marker. Repository and snapshot provenance becomes immutable at readiness. |
| `comparisons` | Comparison id; status is derived from child Runs | Comparison and all initial child Runs are inserted in one transaction. The final child deletion removes an empty Comparison in the same transaction. |
| `instances` | One optional process record per Run | Run deletion cascades to the Instance; Build deletion remains restricted. |
| `artifacts` | `(run_id, kind)` | Stores only relative path, SHA-256, size, and Run relation. Filesystem adapters own all bytes. |
| `metrics` | `(run_id, name)` | Replaced transactionally per successful analysis; Run deletion cascades. |
| `scenario_catalog` | `(source, scenario_id)` | Mutable discovery/cache metadata, not JSB0 semantic authority. |
| `deployments` | Deployment id and explicit lifecycle | Repository transition methods validate domain lifecycle edges; port reservation uses `BEGIN IMMEDIATE`. |

No independent queue table exists. An in-process scheduler is only a bounded wake-up/concurrency mechanism: it cannot transition work to running. A process may execute only after the repository atomically claims the authoritative queued SQLite row.

## Transaction boundaries

- Build reservation uses `BEGIN IMMEDIATE` for canonical BuildKey lookup, attempt insertion, deterministic relative paths, and `build_keys` replacement. Concurrent callers receive the same active attempt.
- Run preparation writes immutable files first, then one DB transaction freezes provenance, registers snapshot metadata, and publishes the readiness marker. Workers cannot claim half-prepared Runs.
- Run and Build claims are compare-and-set updates from queued to running. Duplicate claims are no-ops.
- Comparison creation inserts the Comparison and every child Run in one transaction. A child uniqueness or FK failure rolls back the entire graph.
- Run terminal transitions, metrics replacement, and artifact registration each own a repository transaction.
- Run DB deletion cascades metrics, artifacts, and Instance metadata and deletes an empty parent Comparison in one transaction.

## Provenance and artifact rules

Prepared Run provenance includes repository, branch, commit, Build, contract version, scenario identity/path/hash, parameter snapshot path/hash, execution mode/variants, and parameter values. A SQLite trigger rejects later mutation even if repository code is bypassed. Runtime result fields, lifecycle timestamps, diagnostics, and analysis results remain writable outputs.

Scenario, parameter, Runtime, telemetry, diagnostic, and analysis files live below the configured data root. Artifact rows contain relative paths only. Build workspace paths are also written and returned as workspace-relative paths; download endpoints resolve them through confined filesystem adapters.

## Deletion and partial failure

Terminal Run deletion first atomically renames its directory to an inaccessible staged name. If the DB transaction fails, the directory is restored. After DB success, staged bytes are purged. A purge failure leaves an unreferenced staged directory for maintenance cleanup but never resurrects DB metadata. When the final child Run is deleted, its Comparison row and comparison snapshot directory are removed as orphaned state. Shared Builds and immutable Runtime worktrees are never deleted with a Run.

## Query indexes

Indexes cover the observed filters and polling paths: Run status/readiness/id, Build status/id, Run scenario name, comparison id, repository/branch, and existing Deployment and scenario-catalog queries. No speculative indexes are added beyond these paths.
