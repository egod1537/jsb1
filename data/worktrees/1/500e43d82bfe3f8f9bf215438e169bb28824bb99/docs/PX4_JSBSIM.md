# PX4 + JSBSim Environment

This environment provides a reproducible PX4 Software-In-The-Loop stack backed
by JSBSim flight dynamics. It uses the JSBSim bridge included in PX4 rather than
adding PX4 or its dependencies to the native application build.

```text
QGroundControl / MAVSDK
          ^
          | MAVLink UDP 14550 / 14540
          v
      PX4 SITL
          ^
          | Simulator MAVLink TCP 4560
          v
   PX4 JSBSim bridge
          |
          v
       JSBSim
```

## Supported host environment

- Ubuntu 22.04 or 24.04
- Windows 10/11 with Ubuntu under WSL2
- PX4 v1.17.0
- JSBSim v1.3.0

PX4 and JSBSim are cloned into the Linux filesystem under
`~/.local/share/jsb-test/px4` by default. Keeping these build trees in the WSL2
filesystem avoids the performance and file-permission problems caused by
building Linux projects under `/mnt/c`.

## Windows setup

Run the following once from an Administrator PowerShell terminal:

```powershell
wsl --install -d Ubuntu-24.04
```

Restart Windows when requested. Launch Ubuntu once and create the Linux user,
then run the repository setup from a normal PowerShell terminal:

```powershell
make px4-setup
```

The setup command installs only the packages required for PX4 SITL and the
JSBSim bridge. It does not install the NuttX cross-compiler or Gazebo.

## Commands

```powershell
# Show installed source and binary status
make px4-status

# Rebuild without starting the simulator
make px4-build

# Start PX4 SITL, the bridge, and JSBSim
make px4-run
```

`make px4-run` is interactive. Stop PX4 and the bridge with `Ctrl+C`.

## Configuration

The wrapper recognizes these optional environment variables:

| Variable | Default | Purpose |
| --- | --- | --- |
| `PX4_WSL_DISTRO` | WSL default | Selects a WSL distribution by name. |
| `PX4_WORKSPACE` | `~/.local/share/jsb-test/px4` | Sets the Linux workspace path. |
| `PX4_JSBSIM_MODEL` | `rascal` | Selects `rascal`, `malolo`, `quadrotor_x`, or `hexarotor_x`. |
| `PX4_HEADLESS` | `1` | Set to `0` to allow the PX4 launch script to start FlightGear. |
| `PX4_VERSION` | `v1.17.0` | Selects the PX4 source tag. |
| `JSBSIM_VERSION` | `v1.3.0` | Selects the JSBSim source tag. |

For example:

```powershell
$env:PX4_JSBSIM_MODEL = "quadrotor_x"
make px4-build
make px4-run
```

## Ports

- TCP `4560`: simulator MAVLink connection between PX4 and the JSBSim bridge.
- UDP `14550`: PX4 ground-control connection used by QGroundControl.
- UDP `14540`: PX4 offboard API connection used by MAVSDK.

The PX4/JSBSim environment is currently a standalone integration test stack.
The repository's `make run` command still launches the native C172x console and
does not share its in-process JSBSim instance with PX4.

## Troubleshooting

If a Make target reports that WSL2 is missing, install it using the Windows
setup command above. If multiple WSL distributions are installed, set
`PX4_WSL_DISTRO` to the Ubuntu distribution shown by `wsl --list --verbose`.

If PX4 waits for the simulator on port `4560`, check that no other simulator is
using the port and rerun `make px4-build`. The build status should report both
`PX4 SITL binary: ready` and `JSBSim bridge: ready`.
