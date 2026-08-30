# JSB0 Runtime Architecture

The source tree exposes architectural ownership directly. JSB0 Runtime owns
simulation execution and the external JSB0↔JSB1 contract; desktop and headless
entry points compose those modules without moving domain behavior into the
executable layer.

## Modules

- `src/app` is the desktop composition root. It constructs Runtime, messaging,
  GUI, and integrations and owns startup, scheduling, and shutdown.
- `src/sim` owns simulation state and execution, JSBSim access, control and
  autopilot behavior, scenarios, linearization, and runtime telemetry
  production. It must not depend on GUI or rendering libraries.
- `src/messaging` is the typed process/module boundary between Runtime and GUI.
  It owns the in-process bus, command/event contracts, client cache, and Runtime
  adapter.
- `src/gui` owns the ImGui editor, controller/view features, layouts, windows,
  platform services, and presentation-local state. It sends commands through
  messaging and may consume stable plain simulation contracts.
- `src/flightui` owns dumb ImGui/ImPlot controls plus passive 3D scene, camera,
  and drawing primitives. GUI-specific typed events remain in `src/gui`.
- `src/contract` owns C++ integration for the declarative root `contract/`
  source of truth: generated Protobuf types, recording DTOs, and MCAP adapters.
- `src/integration/flightgear` is an outbound external-display adapter over a
  read-only simulation snapshot.
- `src/runner` is the JSB1-facing headless execution boundary. It loads a
  scenario, composes Runtime, and writes contract artifacts and exit status.
- `src/common` contains only subsystem-independent math, containers, and small
  utilities.

The repository-root `contract/` contains machine-readable Protobuf, JSON
Schema, signal catalog, version, and examples. `src/contract/` contains only
the C++ adapters that implement that specification.

## Dependency direction

```text
                    app
             ┌──────┼───────────┐
             v      v           v
            gui  messaging  flightgear integration
             │      │           │
             v      v           v
          flightui  sim <──── stable snapshots
                     │
                     v
                  contract
                     │
                     v
                   common

runner ────────────> sim + contract
```

`sim` must not include GUI, FlightUI, messaging, or ImGui/GLFW. `common` must
not include any higher-level subsystem. GUI code must not include concrete
`SimulationRuntime`, JSBSim wrappers, or autopilot implementations. FlightUI
may consume stable render-state structs but must not know GUI controllers,
messaging, Runtime, or external contract adapters.

## CMake targets

```text
jsb_common
jsb_contract_proto -> jsb_contract
jsb_sim_core -> jsb_sim_runtime
jsb_message_bus -> jsb_messaging
jsb_gui_architecture + jsb_monitor + flightui -> jsb_editor
jsb_flightgear + jsb_editor -> jsb_app -> jsb-flight-console
jsb_sim_runtime + jsb_contract -> jsb_runner -> jsb-sim-runner
```

`jsb_monitor` remains separate because it has useful headless controller tests.
Other GUI features stay together in `jsb_editor` to avoid target fragmentation.

## Boundary enforcement

`scripts/check_architecture.py` scans source includes for prohibited reverse
dependencies. CTest runs it as `architecture_boundaries`. It is intentionally a
small guardrail rather than a new dependency-analysis framework.

Namespaces retain their established names in this filesystem/CMake refactor.
Future namespace normalization can introduce `jsb::...` incrementally without
combining that source-level churn with ownership moves.
