from __future__ import annotations

import asyncio
import json
import logging
import os
from datetime import datetime, timezone
from pathlib import Path

from app.analysis.mcap_reader import McapReadError, McapRunReader
from app.analysis.roll_hold import compute_roll_hold_metrics
from app.domain.models import Metric, RunStatus
from app.repositories.runs import RunRepository
from app.repositories.builds import BuildRepository
from app.repositories.instances import InstanceRepository
from app.domain.build import BuildStatus
from app.services.runner import SimulationRunner


logger = logging.getLogger(__name__)
REQUIRED_METRIC_CHANNELS = ["commanded_roll", "roll", "aileron"]


def now_iso() -> str:
    return datetime.now(timezone.utc).isoformat()


class RunExecutionService:
    def __init__(
        self,
        repository: RunRepository,
        runner: SimulationRunner,
        reader: McapRunReader,
        data_dir: Path,
        builds: BuildRepository | None = None,
        instances: InstanceRepository | None = None,
        build_root: Path | None = None,
    ) -> None:
        self.repository = repository
        self.runner = runner
        self.reader = reader
        self.data_dir = data_dir.resolve()
        self.builds = builds
        self.instances = instances
        self.build_root = build_root.resolve() if build_root is not None else None

    async def execute(self, run_id: int) -> None:
        run = self.repository.get(run_id)
        try:
            if run.output_directory is None:
                raise RuntimeError("run output directory was not initialized")
            output_directory = (self.data_dir / run.output_directory).resolve()
            output_directory.mkdir(parents=True, exist_ok=True)
            log_path = output_directory / "stdout.log"
            logger.info("run started id=%s scenario=%s", run.id, run.scenario_name)
            self.repository.transition(
                run_id,
                expected=[RunStatus.QUEUED],
                status=RunStatus.RUNNING,
                started_at=now_iso(),
            )
            self._write_manifest(run_id)
            executable_path: Path | None = None
            instance = self.instances.get_for_run(run_id) if self.instances else None
            if run.build_id is not None:
                if self.builds is None:
                    raise RuntimeError("build repository is not configured")
                build = self.builds.get(run.build_id)
                if build.status is not BuildStatus.COMPLETED or not build.executable_path:
                    raise RuntimeError("selected build is not completed")
                executable_path = Path(build.executable_path).resolve()
                if self.build_root is None:
                    raise RuntimeError("build root is not configured")
                build_dir = Path(build.build_dir).resolve()
                try:
                    build_dir.relative_to(self.build_root)
                    executable_path.relative_to(build_dir)
                except ValueError as exc:
                    raise RuntimeError("selected build executable escapes build root") from exc
                if not executable_path.is_file() or not os.access(executable_path, os.X_OK):
                    raise FileNotFoundError("selected build executable is missing")

            def on_started(pid: int) -> None:
                if instance is not None and self.instances is not None:
                    self.instances.mark_running(instance.id, pid)

            result = await self.runner.run(
                scenario_path=Path(run.scenario_path),
                output_directory=output_directory,
                autopilot=run.autopilot,
                log_path=log_path,
                executable_path=executable_path,
                on_started=on_started,
            )
            if log_path.exists():
                self._register(run_id, "stdout", log_path)
            if result.exit_code != 0:
                raise RuntimeError(f"runner exited with code {result.exit_code}")

            telemetry_path = output_directory / "telemetry.mcap"
            if not telemetry_path.is_file():
                raise FileNotFoundError("runner completed without telemetry.mcap")
            self._register(run_id, "telemetry", telemetry_path)

            logger.info("analysis started run_id=%s", run_id)
            timeline, series = self.reader.read_aligned(
                telemetry_path, REQUIRED_METRIC_CHANNELS
            )
            metrics = compute_roll_hold_metrics(
                timeline,
                series["commanded_roll"],
                series["roll"],
                series["aileron"],
            )
            self.repository.replace_metrics(run_id, metrics)
            metrics_path = output_directory / "metrics.json"
            previous = self._read_existing_metrics(metrics_path)
            payload = {
                **previous,
                "metrics": {
                    metric.name: {"value": metric.value, "unit": metric.unit}
                    for metric in metrics
                },
                "definitions": metric_definitions(),
            }
            metrics_path.write_text(
                json.dumps(payload, indent=2, allow_nan=False), encoding="utf-8"
            )
            self._register(run_id, "metrics", metrics_path)
            simulation_time = float(timeline[-1] - timeline[0])
            self.repository.transition(
                run_id,
                expected=[RunStatus.RUNNING],
                status=RunStatus.COMPLETED,
                finished_at=now_iso(),
                exit_code=result.exit_code,
                wall_time_sec=result.wall_time_sec,
                simulation_time_sec=simulation_time,
                error_message=None,
            )
            self._write_manifest(run_id)
            if instance is not None and self.instances is not None:
                self.instances.finish(instance.id, failed=False)
            logger.info("analysis completed run_id=%s", run_id)
        except asyncio.CancelledError:
            await self._fail(run_id, "backend stopped while run was active")
            raise
        except Exception as exc:
            logger.exception("run failed id=%s", run_id)
            exit_code = getattr(locals().get("result"), "exit_code", None)
            wall_time = getattr(locals().get("result"), "wall_time_sec", None)
            await self._fail(run_id, str(exc), exit_code=exit_code, wall_time=wall_time)

    async def _fail(
        self,
        run_id: int,
        message: str,
        *,
        exit_code: int | None = None,
        wall_time: float | None = None,
    ) -> None:
        try:
            run = self.repository.get(run_id)
            expected = [RunStatus.QUEUED, RunStatus.RUNNING]
            self.repository.transition(
                run_id,
                expected=expected,
                status=RunStatus.FAILED,
                finished_at=now_iso(),
                exit_code=exit_code,
                wall_time_sec=wall_time,
                error_message=message[:4000],
            )
            if self.instances is not None:
                instance = self.instances.get_for_run(run_id)
                if instance is not None:
                    self.instances.finish(instance.id, failed=True)
        except Exception:
            logger.exception("could not persist failed status run_id=%s", run_id)
            return
        if run.output_directory:
            try:
                output = (self.data_dir / run.output_directory).resolve()
                output.mkdir(parents=True, exist_ok=True)
                log_path = output / "stdout.log"
                if log_path.exists():
                    self._register(run_id, "stdout", log_path)
                self._write_manifest(run_id)
            except Exception:
                logger.exception("could not finalize failed run artifacts run_id=%s", run_id)

    def _register(self, run_id: int, kind: str, path: Path) -> None:
        relative = path.resolve().relative_to(self.data_dir).as_posix()
        self.repository.upsert_artifact(run_id, kind, relative)

    def _write_manifest(self, run_id: int) -> None:
        run = self.repository.get(run_id)
        if not run.output_directory:
            return
        path = self.data_dir / run.output_directory / "run.json"
        payload = run.model_dump(mode="json")
        payload["repository"] = (
            {"id": run.repository_id, "name": run.repository_name}
            if run.repository_id is not None
            else None
        )
        path.write_text(
            json.dumps(payload, indent=2, allow_nan=False), encoding="utf-8"
        )
        self._register(run_id, "run", path)

    @staticmethod
    def _read_existing_metrics(path: Path) -> dict[str, object]:
        if not path.is_file():
            return {}
        try:
            value = json.loads(path.read_text(encoding="utf-8"))
            return value if isinstance(value, dict) else {}
        except (OSError, json.JSONDecodeError):
            return {}


def metric_definitions() -> dict[str, str]:
    return {
        "settling_time_sec": (
            "Time after the first command change until absolute roll tracking error "
            "remains within ±0.5 deg; null when it never settles."
        ),
        "overshoot_deg": (
            "Largest actual-roll excursion beyond commanded roll in the final command direction."
        ),
        "rms_error_deg": "RMS commanded-minus-actual roll error after command onset.",
        "steady_state_error_deg": (
            "Absolute mean commanded-minus-actual roll error over the last 20% of samples."
        ),
        "max_abs_aileron_deg": "Maximum absolute aileron deflection over the complete run.",
    }
