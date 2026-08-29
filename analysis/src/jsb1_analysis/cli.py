from __future__ import annotations

import argparse
import json

from jsb1_analysis.analyzers.roll_hold import RollHoldAnalyzer, RollHoldConfig
from jsb1_analysis.io.run import load_run


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Analyze one JSB1 roll-hold MCAP artifact"
    )
    parser.add_argument(
        "--mcap", required=True, help="telemetry.mcap or JSB1 run directory"
    )
    parser.add_argument("--start", type=float)
    parser.add_argument("--end", type=float)
    parser.add_argument("--command-signal", default="commanded_roll")
    parser.add_argument("--roll-signal", default="roll")
    parser.add_argument("--roll-rate-signal", default="roll_rate")
    parser.add_argument("--aileron-signal", default="aileron")
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    run = load_run(args.mcap)
    analyzer = RollHoldAnalyzer(
        RollHoldConfig(
            command_signal=args.command_signal,
            roll_signal=args.roll_signal,
            roll_rate_signal=args.roll_rate_signal,
            aileron_signal=args.aileron_signal,
        )
    )
    result = analyzer.analyze(run, start_time=args.start, end_time=args.end)
    print(json.dumps(result.as_dict(), indent=2, allow_nan=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
