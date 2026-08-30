# JSB1 backend

From this directory:

```sh
python3 -m venv .venv
source .venv/bin/activate
python -m pip install -e '.[test]'
uvicorn app.main:app --reload
```

The MCAP telemetry contract used by v0 is documented in the repository root README.

Run creation accepts a scenario and JSB0 branch. Autopilot is read from the scenario,
validated with the schema in the resolved JSB0 worktree, and retained on the Run as
execution provenance; it is not a run-level override.
