# Passive Monitor feature

Monitor is a one-way visualization consumer. The GUI root reads the latest
published snapshots from `SimulationMessageClient`'s cache and constructs a
`MonitorInput`. Monitor never queries runtime or simulation objects.

```text
SimulationRuntime -> MessageBus -> SimulationMessageClient cache
                                      |
                                      v
                                GUI root input adapter
                                      |
                                      v
                                 MonitorInput
                                      |
                              MonitorController
                               /            \
                       MonitorState       MonitorView
                                            |
                              timeline / plots / modes
```

## Dependency audit

Visualization inputs (category A):

- immutable primary and baseline `TelemetrySnapshot` instances;
- an immutable dynamic-mode history view and update-status values supplied in
  `MonitorDynamicModeInput`;
- telemetry path constants used to select snapshot series.

Visualization-local state (category B):

- live/frozen state, total/view/visible ranges, cursor, selection and ticks;
- plot definitions, visibility, layout and selected telemetry channels;
- pane sizes, browser filter, timeline drag state and selected dynamic mode.

Commands/dependencies removed from Monitor (category C):

- `SimulationMessageClient` and its per-frame getters;
- the automatic-linearization command call;
- simulation/runtime/controller objects and mutable telemetry registries.

The automatic-linearization checkbox now emits
`MonitorAutomaticLinearizationChanged` to the GUI parent. The parent decides
which application controller handles that intent.

## State and interaction rules

`MonitorController` is the single owner of `MonitorState`. `MonitorView`
receives immutable state, edits a per-frame presentation copy, and emits typed
`MonitorEvent` values upward. Every plot receives the same timeline object;
plots never own independent live, viewport, visible-range, or cursor state.

Telemetry and dynamic-mode snapshots are authoritative read-only inputs. They
are not copied into `MonitorState`. Dynamic-mode identification and eigenmode
calculation remain outside the GUI; Monitor only renders the supplied history.

The controller logic is built as the headless `jsb_monitor` target so timeline
behavior can be tested without ImGui, `SimulationRuntime`, or
`TelemetryRegistry`.
