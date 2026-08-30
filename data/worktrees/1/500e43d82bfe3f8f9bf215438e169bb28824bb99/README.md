# JSB Flight Console

A real-time flight simulation console that runs JSBSim and visualizes the aircraft state in FlightGear.

The user can control a Cessna 172 with the keyboard while JSBSim calculates the flight dynamics at 120 Hz.

## Environment

* Ubuntu 24.04 LTS
* C++20 compiler
* CMake 3.28 or later
* Ninja
* Git
* FlightGear AppImage

`ccache` is optional. When it is available on `PATH`, CMake uses it
automatically for C++ compilation.

## Installation

Install the required build tools:

```bash
sudo apt update
sudo apt install -y build-essential cmake ninja-build git libfuse2
```

Download the Linux x86_64 AppImage from the [official FlightGear download page](https://www.flightgear.org/download/).

Create the dependency directory:

```bash
mkdir -p .deps/flightgear
```

Move the downloaded AppImage into the project and rename it:

```bash
mv ~/Downloads/<downloaded-flightgear-file>.AppImage \
  .deps/flightgear/flightgear.AppImage
```

Make the AppImage executable:

```bash
chmod +x .deps/flightgear/flightgear.AppImage
```

## Build

Configure the project:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
```

Build the application:

```bash
cmake --build build
```

Build only the console target when needed:

```bash
cmake --build build --target jsb-flight-console
```

Run the tests:

```bash
ctest --test-dir build
```

Simulation telemetry can also be persisted as standard MCAP files from the
Simulation Control toolbar. See [MCAP Telemetry Recording](docs/MCAP_RECORDING.md)
for the channel schema, metadata, and reader/playback boundary.

The interactive GUI communicates with the simulation runtime through a
type-safe, synchronous C++ publish/subscribe layer. See
[In-Process Simulation Message Bus](docs/MESSAGE_BUS.md) for its contracts and
dispatch/lifetime semantics.

The configure step automatically downloads the required JSBSim source code.
The equivalent `make configure`, `make build`, and `make test` commands remain
available.

Unity builds are optional and disabled by default. Enable them in a separate
build directory when faster clean builds are preferred over fine-grained
incremental compilation:

```bash
cmake -S . -B build -G Ninja -DJSB_ENABLE_UNITY_BUILD=ON
```

## Run

Open two terminals.

Start FlightGear in the first terminal:

```bash
make fg
```

Wait until FlightGear finishes loading.

Start the JSBSim simulation in the second terminal:

```bash
make run
```

Press `Ctrl+C` in each terminal to stop the programs.

## Roll Hold comparison

The application runs two independent C172x simulations at the same fixed
timestep. `Primary` uses the native controller and `Baseline` uses the embedded
PX4 v1.17 fixed-wing Roll Hold law. Use the source selector in the GNC
Autopilot tab to inspect and tune either strategy. The two Flight Viz windows
show their aircraft separately; each view can also overlay the other aircraft
as a shadow.

Interactive operation shares manual pilot input between the simulations while
keeping each autopilot's state and telemetry independent. Running a scenario
resets both simulations to the same initial condition and applies the same Roll
Hold target and command timing. Open Monitor and enable the `Roll Hold` preset
to overlay the Primary and Baseline roll, rate, and aileron histories.

The embedded controller follows PX4 v1.17's fixed-wing attitude/rate control
equations. PX4 roll torque is mapped directly to the C172x normalized aileron
direction; the Rascal bridge's servo-channel reversal is intentionally not
applied. Its default gains use the tuned C172x profile
(`FW_R_TC=0.35`, `FW_RR_P=0.160`, `FW_RR_I=0.080`,
`FW_RR_FF=0.80`, and `FW_RR_IMAX=0.15`). This mode executes the PX4 control law
in-process; use the standalone PX4 environment below when validating the
complete PX4 SITL binary, estimator, flight mode, control allocation, and
MAVLink behavior.

With `Baseline` selected, expand `PX4 v1.17 Reference Tuning` to adjust the
corresponding `FW_R_*` and `FW_RR_*` values live. `Reset C172x PX4 Tuning`
restores the tuned profile. If roll oscillation remains for a changed flight
condition, increase `FW_R_TC` first or reduce `FW_RR_P`/`FW_RR_FF`; keep
`FW_RR_I` low and raise it only to remove a persistent steady-state offset.

Re-run the deterministic C172x `+10/-10 deg` tuning benchmark after changing
the controller or its defaults:

```powershell
cmake --build build --target px4_roll_tuning_probe
.\build\px4_roll_tuning_probe.exe
```

## PX4 + JSBSim SITL

The repository includes a reproducible PX4 SITL environment that connects PX4
to JSBSim through the simulator MAVLink interface on TCP port `4560`. On
Windows, PX4 runs in Ubuntu under WSL2; the native application build remains on
Windows.

Install WSL2 from an Administrator PowerShell terminal when it is not already
available:

```powershell
wsl --install -d Ubuntu-24.04
```

Restart Windows and launch Ubuntu once to finish creating the Linux user. Then
return to this repository and install PX4 v1.17.0, JSBSim v1.3.0, and the bridge
dependencies:

```powershell
make px4-setup
```

Build or run the fixed-wing SITL environment:

```powershell
make px4-build
make px4-run
```

The default vehicle is the PX4-supported Rascal fixed-wing model and the bridge
runs headless. See [PX4 + JSBSim Environment](docs/PX4_JSBSIM.md) for the
architecture, configuration variables, QGroundControl ports, and
troubleshooting.

## Controls

## Headless simulation runner

Build the core runner without the interactive editor and execute a scenario at
fixed simulation timestep:

```powershell
cmake -S . -B build-headless -G Ninja -DJSB_BUILD_EDITOR=OFF -DBUILD_DOCS=OFF
cmake --build build-headless --target jsb-sim-runner
build-headless/jsb-sim-runner.exe --scenario scenarios/c172_roll_hold_5deg.yaml --output results/roll_hold_5deg
```

The runner performs no realtime sleeping or GUI initialization and writes a
contract-valid `run.json` plus Protobuf telemetry in `telemetry.mcap`. Runtime
scenario, telemetry, metadata, signal semantics, generation, and compatibility
rules are defined in [`contract/README.md`](contract/README.md).

| Key | Action            |
| --- | ----------------- |
| `W` | Pitch down        |
| `S` | Pitch up          |
| `A` | Roll left         |
| `D` | Roll right        |
| `Q` | Yaw left          |
| `E` | Yaw right         |
| `R` | Increase throttle |
| `F` | Decrease throttle |

Each key press changes the corresponding normalized control input by `0.05`.

## Simulation Settings

* Aircraft: Cessna 172 (`c172x`)
* Simulation frequency: 120 Hz
* Initial altitude: 1,000 ft above sea level
* Initial calibrated airspeed: 80 kt
* FlightGear UDP address: `127.0.0.1:5500`
