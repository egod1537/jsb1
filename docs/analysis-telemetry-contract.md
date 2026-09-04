# Analysis and telemetry contract boundary

## Duplicate implementation audit

Before this refactor the backend and `analysis` package were separate analysis
stacks:

| Concern | Backend copy | Offline package copy |
| --- | --- | --- |
| MCAP decode | `app/analysis/mcap_reader.py` decoded JSON, float64, and protobuf | `jsb1_analysis/io/mcap.py` decoded JSON and float64 only |
| Logical signals | backend aliases, protobuf fields, and `RuntimeSignalCatalog` projection | `io/run.py` aliases and analyzer-local signal names |
| Alignment | backend reader interpolation | package loader interpolation into `RunData` |
| Roll metrics | `app/analysis/roll_hold.py` plus the detailed roll-hold analyzer | `metrics/time_response.py` and the package roll-hold analyzer |
| Variants | inferred from `/jsb/<variant>/...` topics | unsupported |
| Units | backend analysis converted wire radians while computing several metrics | package kept input units but had no contract metadata |

The remaining signal aliases and protobuf field table in the shared package are
an explicitly pre-index compatibility path. Indexed revisions always supply the
exact signal catalog and descriptor; their topic, field, unit, and variant
semantics never use that table.

## Current ownership

`jsb1_analysis.io.mcap.load_dataset()` is now the decoder source of truth. It
combines the exact revision's `signals.yaml`, FileDescriptorSet, and ordered
variant catalog into an immutable `TelemetryDataset`. An analyzer asks only for
logical signals and receives timestamps, values, and unit metadata; it does not
know MCAP topics, protobuf fields, or message types.

The backend `McapRunReader` is a cache/error adapter over that implementation.
Telemetry queries, persisted metric processing, and interactive analysis reuse
one decoded dataset. Presentation conversion remains in the API query layer;
metric primitives calculate on the contract wire SI values.

Reusable primitives live in `jsb1_analysis.metrics`. Both backend roll metric
paths and the notebook-facing analyzer call these functions. Frequency analysis
also delegates to the package implementation. The detailed backend result model
continues to add UI assessments, thresholds, regions, and markers without
copying the numeric primitives.

## Extension and failure model

`AnalyzerRegistry` dispatches by `scenario_type` and validates required logical
signals before invoking an analyzer. `UnsupportedAnalyzerError` means there is
no registered analyzer for that scenario type. `AnalyzerMissingSignalError`
means the analyzer exists but the selected variant lacks contract-required
inputs. Those are intentionally separate compatibility outcomes.

`RunBundle` is the offline immutable view of run metadata, scenario snapshot,
parameter snapshot, telemetry, and exact contract metadata. Its loader discovers
all artifact filenames through `contract/index.json` and `artifacts.json`,
rejects unsafe paths and unsupported contract majors, and never consults a
mutable branch or current checkout.

Adding Pitch Hold, Course, or TECS analysis therefore requires a new analyzer
registered under its scenario type and a logical required-signal set. It does
not require another MCAP decoder, field map, unit table, or variant list.
