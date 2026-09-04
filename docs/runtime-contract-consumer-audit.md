# JSB0 Runtime contract consumer audit

JSB0 is the semantic source of truth. JSB1 owns orchestration and presentation
only. An indexed Runtime revision is consumed from its immutable Git worktree;
no file from the JSB0 contract is copied into this repository.

## Consumer inventory

| Area | Previous mirror / direct dependency | Contract-driven boundary |
| --- | --- | --- |
| Scenario validation | `scenario_validator.py`, `scenarios.py`, scenario sync/write/inspection and `scripts/validate-scenarios.py` | `RuntimeContractReader.load_scenario_schema()` and the parameter catalog from the same worktree |
| Run and comparison creation | Branch capability checks, scenario whitelist, temporary PX4 parameter metadata | Resolve branch → commit → worktree → complete `RuntimeContractBundle` before creating rows |
| Execution mode and variants | `compare`, `baseline`, and `primary` assumptions in creation and the New Run form | `capabilities.json` plus `variants.json`; the UI renders returned variant ids |
| Parameters | PX4 Roll ids, defaults, bounds, units, and source-header parsing in the normal service | Typed `RuntimeParameterDefinition` values from `parameters.json`; values validated by `parameter-set.schema.json`; scenario whitelist intersection enforced separately |
| Runtime artifacts | `run.json`, `scenario.yaml`, `telemetry.mcap`, and `parameters.yaml` assembled in creation/execution/runner services | Paths resolved by artifact type from `artifacts.json` |
| Run metadata | Ad-hoc ingestion of the runner manifest | Exact revision `run.schema.json` validation before ingestion |
| Telemetry decode | Local protobuf message/field map and embedded MCAP schema only | Exact exported FileDescriptorSet plus field mappings derived from `signals.yaml`; an indexed source tree may defer to the MCAP-embedded descriptor only when capabilities explicitly guarantee it |
| Analysis and plots | Backend signal units/groups and frontend signal metadata table | Available-signal API projects topic, field, unit, frame, axis, sign, group, description, and range from the exact run contract; frontend prefers this response |
| Contract version | Optional `version.json` read | Semantic `contract/VERSION` major validation before any indexed semantic file is consumed |
| CI | Fixed scenario schema path and JSB0 `main` wording | Script goes through the reader; workflow checks out the configured JSB0 `impl` contract |

## Ownership and compatibility

`RuntimeContractReader` is the only indexed filesystem-layout owner. It treats
`contract/index.json` as the discovery root, rejects unsafe paths, constructs a
frozen bundle, and caches it only by `(repository_id, commit_sha)`.

Pre-index revisions remain available through `LegacyRuntimeContractAdapter`.
The old PX4 metadata and protobuf field tables are explicitly legacy-only in
`legacy_controller_parameters.py` and the shared analysis package's legacy
decoder path; indexed revisions can never enter those paths. JSB1-owned artifacts such as logs, derived metrics,
and `jsb1-run.json` are intentionally outside the JSB0 artifact contract.

Plot presets and the Roll Hold analyzer still name the signals they need as
feature configuration. They do not define wire fields, units, frames, ranges,
parameter bounds, execution capabilities, or artifact locations; availability
and semantics come from the exact contract catalog at runtime.
