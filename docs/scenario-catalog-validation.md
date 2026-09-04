# Scenario catalog, validation, and remote synchronization

## Ownership

All scenario sources implement the domain `ScenarioSource` port. The port exposes
only `source_type`, `list()`, and `read(id)`. `DirectoryScenarioSource` owns local
bundled/managed filesystem access, `SftpScenarioSource` owns SFTP access, and
`CatalogCachedScenarioSource` reads the active validated remote object selected by
SQLite. Source adapters do not parse YAML or interpret JSB0 fields.

`ScenarioValidator` is the only YAML parsing and JSON Schema validation core. Its
`ScenarioEvaluation` returns the parsed `ScenarioDocument`, structured validation
result, and the policy used. Catalog listing, HTTP validation, managed publishing,
SFTP synchronization, bundled CI, inspection, and Run creation all use this core.
Inspection derives overview and definition data from the same parsed document that
was validated; the browser receives that model and does not parse YAML.

## Two contract policies

- `CATALOG_STABLE` resolves the configured JSB0 repository's default branch to an
  immutable commit and uses that commit for catalog compatibility, HTTP validation,
  managed writes, remote synchronization, and current catalog inspection.
- `RUN_EXACT` is used only after Run/Comparison creation has resolved its selected
  ref to an immutable commit and prepared that commit's worktree. It revalidates the
  selected scenario bytes with the exact worktree contract before snapshotting or
  reserving execution work.

A successful stable validation is never treated as authorization to execute against
a different revision. Conversely, historical inspection uses the frozen Run commit
and snapshot, not today's stable catalog contract.

## Managed publishing

Managed paths must be relative YAML paths. Absolute paths, parent traversal,
backslashes, NULs, and symlink escapes are rejected. The full text is validated with
the stable policy before publication. Publication uses a temporary file and a
same-filesystem link, so create never overwrites an existing scenario. Run snapshots
are separate immutable files under the Run's artifact layout.

## Remote synchronization transaction

Each successful remote object follows this order:

1. Read remote bytes into temporary process memory.
2. Decode UTF-8 and evaluate once with the stable contract.
3. Compute SHA-256.
4. Atomically publish an immutable content-addressed object below
   `remote/objects/<prefix>/<sha256>.yaml`.
5. Atomically switch the SQLite catalog row to that relative object path.

An invalid update records structured errors but retains the prior valid catalog row
and content object. If database publication fails, the previous row still points to
the previous object; the newly written object is only an unreferenced cache object.
An SFTP outage changes sync health only and never removes cached entries. Missing
remote IDs are marked inactive only after a successful remote listing. Their cached
bytes may remain, while inactive entries disappear from the active catalog.

Run scenario snapshots are copied before execution and are owned by the Run artifact
store. Remote deletion or later cache updates therefore cannot change historical Run
provenance.

## Transaction boundaries

SQLite commits each catalog row transition and sync-state transition atomically.
Filesystem bytes remain filesystem-owned; SQLite stores only the relative cache
pointer and metadata. Cross-filesystem/database atomicity is achieved by immutable
content publication before the database pointer switch, never by overwriting the
currently referenced object.
