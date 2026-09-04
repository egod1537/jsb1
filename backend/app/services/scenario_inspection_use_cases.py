from __future__ import annotations

import asyncio

from app.domain.errors import ApplicationError
from app.domain.scenario import ScenarioInspectionCatalogEntry, ScenarioInspectionDetail
from app.services.repository_manager import RepositoryManager
from app.services.scenario_inspection import ScenarioInspectionService
from app.services.scenario_snapshot_queries import ScenarioSnapshotQueryService
from app.services.scenario_sync import StableScenarioContractResolver
from app.services.scenario_validator import ScenarioContract, ScenarioValidator


class ScenarioContractUnavailable(ApplicationError):
    pass


class ScenarioInspectionUseCases:
    """Application orchestration for current and historical scenario inspection."""

    def __init__(
        self,
        inspector: ScenarioInspectionService,
        resolver: StableScenarioContractResolver,
        validator: ScenarioValidator,
        repositories: RepositoryManager,
        snapshots: ScenarioSnapshotQueryService,
    ) -> None:
        self.inspector = inspector
        self.resolver = resolver
        self.validator = validator
        self.repositories = repositories
        self.snapshots = snapshots

    async def catalog(self) -> list[ScenarioInspectionCatalogEntry]:
        compatibility, contract = await self._current_contract()
        return await asyncio.to_thread(
            self.inspector.catalog, contract, compatibility.runtime
        )

    async def detail(self, source: str, scenario_id: str) -> ScenarioInspectionDetail:
        compatibility, contract = await self._current_contract()
        return await asyncio.to_thread(
            self.inspector.detail,
            source,
            scenario_id,
            contract,
            compatibility.runtime,
        )

    async def run_snapshot(self, run_id: int) -> ScenarioInspectionDetail:
        snapshot = self.snapshots.for_run(run_id)
        contract = await self._historical_contract(
            snapshot.repository_id, snapshot.commit_sha
        )
        return self.inspector.snapshot_detail(
            scenario_id=str(snapshot.id),
            name=snapshot.name,
            relative_path=snapshot.relative_path,
            yaml_text=snapshot.yaml_text,
            expected_sha256=snapshot.sha256,
            authority="frozen run snapshot",
            contract=contract,
            runtime_branch=snapshot.branch,
            runtime_commit=snapshot.commit_sha,
        )

    async def comparison_snapshot(
        self, comparison_id: int
    ) -> ScenarioInspectionDetail:
        snapshot = self.snapshots.for_comparison(comparison_id)
        contract = await self._historical_contract(
            snapshot.repository_id, snapshot.commit_sha
        )
        return self.inspector.snapshot_detail(
            scenario_id=f"comparison-{snapshot.id}",
            name=snapshot.name,
            relative_path=snapshot.relative_path,
            yaml_text=snapshot.yaml_text,
            expected_sha256=snapshot.sha256,
            authority="frozen comparison snapshot",
            contract=contract,
            runtime_branch=snapshot.branch,
            runtime_commit=snapshot.commit_sha,
        )

    async def _current_contract(self):
        try:
            compatibility = await asyncio.to_thread(self.resolver.resolve)
            contract = await asyncio.to_thread(
                self.validator.load_runtime_contract, compatibility.runtime_root
            )
        except Exception as exc:
            raise ScenarioContractUnavailable(
                "Configured JSB0 scenario contract is unavailable"
            ) from exc
        return compatibility, contract

    async def _historical_contract(
        self, repository_id: int | None, commit_sha: str | None
    ) -> ScenarioContract | None:
        if repository_id is None or not commit_sha:
            return None
        try:
            worktree = await asyncio.to_thread(
                self.repositories.prepare_worktree, repository_id, commit_sha
            )
            return await asyncio.to_thread(
                self.validator.load_runtime_contract, worktree
            )
        except Exception:  # noqa: BLE001 - historical inspection is best effort
            return None
