#!/usr/bin/env python3
"""Run baseline and primary concurrently against one exact Scenario."""

from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import subprocess
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--runner", type=Path, required=True)
    parser.add_argument("--scenario", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    arguments = parser.parse_args()

    if arguments.output.exists():
        shutil.rmtree(arguments.output)
    arguments.output.mkdir(parents=True)
    scenario_bytes = arguments.scenario.read_bytes()
    scenario_digest = hashlib.sha256(scenario_bytes).hexdigest()

    processes: dict[str, subprocess.Popen[str]] = {}
    for variant in ("baseline", "primary"):
        output = arguments.output / variant
        processes[variant] = subprocess.Popen(
            [
                str(arguments.runner),
                "--scenario",
                str(arguments.scenario),
                "--variant",
                variant,
                "--output",
                str(output),
            ],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )

    failures: list[str] = []
    for variant, process in processes.items():
        stdout, stderr = process.communicate(timeout=120)
        output = arguments.output / variant
        if process.returncode != 0:
            failures.append(
                f"{variant} exited {process.returncode}\n{stdout}\n{stderr}"
            )
            continue
        for artifact in ("telemetry.mcap", "run.json", "scenario.yaml"):
            if not (output / artifact).is_file():
                failures.append(f"{variant} did not produce {artifact}")
        if failures:
            continue
        metadata = json.loads((output / "run.json").read_text(encoding="utf-8"))
        if metadata.get("execution", {}).get("variant") != variant:
            failures.append(f"{variant} run.json has the wrong variant")
        if metadata.get("scenario", {}).get("digest_sha256") != scenario_digest:
            failures.append(f"{variant} run.json has the wrong Scenario digest")
        if (output / "scenario.yaml").read_bytes() != scenario_bytes:
            failures.append(f"{variant} Scenario snapshot differs from input")

    if failures:
        raise RuntimeError("\n".join(failures))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
