# JSB1 backend

From this directory:

```sh
python3 -m venv .venv
source .venv/bin/activate
python -m pip install -e '.[test]'
uvicorn app.main:app --reload
```

The MCAP telemetry contract used by v0 is documented in the repository root README.

