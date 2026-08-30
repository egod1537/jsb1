# MCAP Telemetry Recording

The application records the existing simulation telemetry snapshot as a
persistence consumer. The live Monitor continues to read the same telemetry
registries and does not own the recorder lifecycle.

```text
Simulation telemetry registry
  |-- Live Monitor
  `-- TelemetryRecordingService -> McapTelemetryRecorder -> *.mcap
                                                        -> McapRecordingReader
```

Use `Record` in the Simulation Control toolbar to start a recording. The
button changes to `Stop Recording mm:ss` while active. `Folder` opens the
default `recordings/` directory. Closing the Monitor does not stop recording;
normal application shutdown finalizes an active file.

## Format

The dependency is the official `foxglove/mcap` C++ library, pinned through
CMake FetchContent to `releases/cpp/v2.1.3`. Files use standard chunked MCAP
writing with summary records and message indexes. The initial implementation
uses uncompressed chunks, `json` message encoding, and `jsonschema` schemas.

Both `log_time` and `publish_time` are rounded simulation timestamps in
nanoseconds. Wall-clock creation time is stored only as run metadata. JSON
angles use radians, angular rates use radians per second, and actuator values
are normalized commands.

Registered topics are:

- `/primary/aircraft/state`
- `/primary/control/input`
- `/primary/roll_hold/diagnostics`
- `/primary/roll_hold/settings`
- `/baseline/aircraft/state`
- `/baseline/control/input`
- `/baseline/roll_hold/diagnostics`
- `/baseline/roll_hold/settings`
- `/scenario/event`

Schemas and channels are registered once, on the first valid payload for that
topic, and their IDs are cached for subsequent writes. This also keeps an empty
recording standards-compliant by avoiding unused summary definitions.

Run metadata includes the format and application versions, Git commit,
aircraft, scenario name/file/duration, simulation timestep, primary and
baseline autopilots, wall-clock creation time, build type, platform, and
compiler. Git metadata falls back to `unknown` when configuration is performed
outside a Git worktree.

## Playback seam

`McapRecordingReader` exposes application-owned `RecordedRunInfo`,
`RecordedChannelInfo`, and `RecordedSample` values. It supports summary and
metadata loading, topic filtering, simulation-time ranges, and log-time ordered
iteration without exposing MCAP types to GUI code.

`IMonitorDataSource`, `LiveTelemetryDataSource`, and
`RecordedTelemetryDataSource` establish the source boundary for a later
Live/Recorded source selector. Full seek/playback UI is intentionally deferred;
the existing Monitor remains in live mode.

## Verification

Build and run the MCAP tests:

```text
cmake --build build --target mcap_recording_tests
ctest --test-dir build -R mcap_recording_tests --output-on-failure
```

The integration test leaves a short standard file at
`build/test-results/telemetry.mcap`. It can be inspected with standard MCAP
tools such as `mcap info`, Foxglove, or an MCAP-capable PlotJuggler build.
