#!/usr/bin/env python3
"""Reject source includes that cross the documented JSB0 module boundaries."""

from __future__ import annotations

import argparse
import re
from pathlib import Path


SOURCE_SUFFIXES = {".cpp", ".h", ".hpp", ".inl"}
INCLUDE = re.compile(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]')

RULES: dict[str, tuple[re.Pattern[str], ...]] = {
    "sim": tuple(
        re.compile(pattern)
        for pattern in (
            r"^(gui|flightui|app|runner|messaging|integration)/",
            r"^(imgui|implot|GLFW)(/|\.h)",
        )
    ),
    "common": tuple(
        re.compile(pattern)
        for pattern in (
            r"^(sim|gui|flightui|app|runner|messaging|contract|integration)/",
            r"^(imgui|implot|GLFW)(/|\.h)",
        )
    ),
    "contract": tuple(
        re.compile(pattern)
        for pattern in (
            r"^(sim|gui|flightui|app|runner|messaging|integration)/",
            r"^(imgui|implot|GLFW)(/|\.h)",
        )
    ),
    "flightui": tuple(
        re.compile(pattern)
        for pattern in (
            r"^(gui|messaging|contract|app|runner|integration)/",
            r"^sim/(runtime|gnc|jsbsim)/",
            r"^sim/Simulation(?:\.hpp|\.h)$",
        )
    ),
    "messaging": tuple(
        re.compile(pattern)
        for pattern in (
            r"^(gui|flightui|app|runner|integration)/",
            r"^(imgui|implot|GLFW)(/|\.h)",
        )
    ),
    "gui": tuple(
        re.compile(pattern)
        for pattern in (
            r"^sim/runtime/SimulationRuntime(?:\.hpp|\.h)$",
            r"^sim/Simulation(?:\.hpp|\.h)$",
            r"^sim/(jsbsim|gnc/autopilot)/",
        )
    ),
}


def check(root: Path) -> list[str]:
    violations: list[str] = []
    source_root = root / "src"
    for module, forbidden in RULES.items():
        module_root = source_root / module
        if not module_root.is_dir():
            violations.append(f"missing module directory: src/{module}")
            continue
        for path in sorted(module_root.rglob("*")):
            if not path.is_file() or path.suffix not in SOURCE_SUFFIXES:
                continue
            for line_number, line in enumerate(
                path.read_text(encoding="utf-8").splitlines(), start=1
            ):
                match = INCLUDE.match(line)
                if not match:
                    continue
                include = match.group(1)
                if any(pattern.search(include) for pattern in forbidden):
                    relative = path.relative_to(root).as_posix()
                    violations.append(f"{relative}:{line_number}: forbidden include {include}")
    return violations


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    args = parser.parse_args()
    violations = check(args.root.resolve())
    if violations:
        print("\n".join(violations))
        return 1
    print("architecture include boundaries: passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
