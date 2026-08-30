# Reproducible scenarios

Each YAML file in this directory defines JSB0 experiment conditions and is
validated against `contract/scenario/scenario.schema.json` before the first
simulation tick. Aircraft, initial condition, timestep, duration,
trim/environment choice, ordered commands, and acceptance criteria come only
from the Scenario. The Execution Variant is supplied separately.

Run the same Scenario with either variant and the same built executable:

```text
jsb-sim-runner --scenario scenarios/roll_hold_5deg_30s.yaml --variant baseline --output out/baseline
jsb-sim-runner --scenario scenarios/roll_hold_5deg_30s.yaml --variant primary --output out/primary
```

The reproducibility identity is the immutable JSB0 commit SHA, the exact
Scenario bytes recorded as a SHA-256 digest and snapshot, and the resolved
Execution Variant. The runner requires `--variant`; it has no hidden default.
