# Simulation/GUI Message Bus Refactor Note

작성일: 2026-08-29  
브랜치: `backend`

## Architecture

The interactive application composes a pure C++ synchronous message path:

```text
GUI
  <-> SimulationMessageClient
  <-> MessageBus
  <-> SimulationMessageAdapter
  <-> SimulationRuntime
  <-> Simulation Core
```

The headless runner continues to use `SimulationRuntime` directly.

## Boundaries

- `SimulationRuntime` owns simulation lifecycle, scenarios, primary/baseline
  instances, trim, linearization, and telemetry recording.
- `SimulationMessageAdapter` translates typed commands into runtime calls and
  publishes plain-data status, snapshot, telemetry, and result events.
- `SimulationMessageClient` publishes GUI commands and maintains local event
  caches. GUI rendering only reads those caches.
- `TelemetryRegistry` remains internal. GUI plotting consumes immutable
  `TelemetrySnapshot` data assembled from `TelemetryFrame` events.
- `MessageBus` dispatch is synchronous and in-process. Subscription lifetime is
  explicit and RAII-based; removing a subscription during publication prevents
  later invocation from the current callback snapshot.

## Build targets

```text
jsb_sim_core
  <- jsb_sim_runtime
  <- jsb_messaging -> jsb_message_bus
  <- jsb_editor

jsb_sim_runtime <- jsb_runner
```

The application has no external messaging middleware or generated-interface
build step.

## Validation

Run:

```powershell
cmake --build build -j 8
ctest --test-dir build --output-on-failure
```

Result: the complete Debug build succeeded and all 22 tests passed, including
message-bus unit/integration coverage, scenario execution, telemetry, MCAP,
and the headless smoke test.

For the GUI-free path:

```powershell
cmake -S . -B build-headless-check -G Ninja -DJSB_BUILD_EDITOR=OFF `
  -DBUILD_DOCS=OFF
cmake --build build-headless-check --target jsb_sim_runtime jsb-sim-runner
ctest --test-dir build-headless-check --output-on-failure
```

Result: the editor-free runtime/runner build succeeded and all three headless
runner checks passed.
