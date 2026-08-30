# GUI feature architecture

Application GUI code uses small hierarchical features. The lightweight
`Component`/`Window` lifecycle remains as the editor's rendering shell, but it
no longer exposes `GUI&` as a service locator. Components receive only a
`GUIFrameContext`, and windows receive the immutable `SimulationSnapshot`.

```text
application dependency or authoritative snapshot
                    |
                    v
             feature controller
                    |
             model + plain props
                    v
              view / element
                    |
          typed interaction event
                    v
             feature controller
                    |
       local update, command, or semantic event
                    v
        dependency or parent controller
```

## Responsibilities

- A model owns local UI state: selected tabs, pending edits, validation state,
  expanded sections, and unavoidable input caches. Authoritative simulation
  state stays in snapshots/props unless the user is editing a pending copy.
- A view receives a `const` model, plain immutable props, and an `EventSink`.
  It renders with flightui/ImGui/ImPlot and emits interaction events. It does
  not receive `GUI&`, mutate the model, or access application dependencies.
- A controller owns its model and explicit application dependencies. It builds
  props, handles view/child events in one place, updates local state, sends
  commands, and may emit a higher-level event to its parent.
- flightui remains the low-level rendering toolkit. Application feature state,
  services, simulation commands, and feature controllers stay under `gui`.

A dumb element may keep rendering-local state only when the immediate-mode
toolkit requires it. It must not know `SimulationMessageClient`,
`SimulationRuntime`, global GUI state, arbitrary services, or its parent
controller. Pass values rather than writable references:

```text
model value -> element props -> interaction event -> controller -> model update
```

## Events and nesting

`EventSink<T>` is a local synchronous callback, not an application message bus.
The owner of a view or child feature supplies it and must outlive synchronous
use of callbacks that capture the owner.

Name element-boundary events after interactions, such as
`SliderValueChanged` or `AutopilotSourceSelected`. Name feature-boundary events
after intent or meaning, such as `TargetRollChanged`,
`AutopilotSelectionChanged`, or `SimulationStartRequested`.

A parent controller may own child controllers. Each child owns its model,
handles its low-level view events, and emits only semantic events upward. The
parent therefore does not need to know which button, slider, or other element
produced the change. Dependencies stop at the nearest controller that uses
them; do not pass a giant GUI context down the tree.

## Dependency direction

Allowed:

```text
View -> flightui / ImGui / ImPlot
Controller -> View, Model, explicit application dependencies
Parent Controller -> Child Controller
```

Not allowed:

```text
View -> SimulationMessageClient or SimulationRuntime
Element -> application service or parent dependency
Simulation -> GUI
```

Recommended feature layout:

```text
gui/features/<feature>/
  <Feature>Model.hpp
  <Feature>Events.hpp
  <Feature>View.hpp/.cpp
  <Feature>Controller.hpp/.cpp
```

## Application feature tree

The GUI root owns explicit feature controllers and injects them into the
existing editor windows:

```text
GUI root
  SimulationController
    initial-condition child state
    ScenarioController
      pending draft and file state
      file serialization
      semantic launch events to SimulationController
  GNCController
    manual-control view
    primary roll-hold view
    baseline roll-hold view
    AutopilotSelectorController
  LinearizationController
  MonitorController
    MonitorState (one shared timeline model for every plot)
    passive MonitorInput snapshot contract
    MonitorView (timeline, plot, and mode rendering)
  FlightVizWindow controller boundary
    FlightVisualizer dumb renderer
  EditorPlatformController
    layout model, commands, file dialog, and platform operations
```

The windows remain responsible for preserving the established ImGui layout,
labels, docking, and plot behavior. They render immutable snapshots and route
typed interaction events to their controller. Application commands and
platform operations are performed only by those controllers.

`features/autopilot_selector` is the minimal nested-feature reference. Its view
keeps the existing selector rendering, its controller owns selection mutation,
and it translates interaction-level `AutopilotSourceSelected` into the semantic
parent event `AutopilotSelectionChanged`.

## Compatibility

`Component` and `Window` are retained because they still provide lifecycle,
visibility, persistence, and editor registration. They are compatibility
shells rather than dependency boundaries: neither API grants access to the GUI
root or application services. New application behavior belongs in a feature
controller, while rendering-only state such as an immediate-mode foldout may
remain local to its view shell.
