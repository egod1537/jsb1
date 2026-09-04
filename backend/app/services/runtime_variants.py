from __future__ import annotations

from pathlib import Path

from app.services.runtime_contract import (
    HeadlessExecutionCapabilities,
    RuntimeContractError,
    RuntimeContractReader,
)


class RuntimeVariantContractError(ValueError):
    pass


class RuntimeVariantService:
    """Read execution capabilities owned by one immutable JSB0 checkout."""

    def __init__(self, reader: RuntimeContractReader | None = None) -> None:
        self.reader = reader or RuntimeContractReader()

    def variants(self, runtime_worktree: Path) -> list[str]:
        return list(self.capabilities(runtime_worktree).variants)

    def capabilities(
        self, runtime_worktree: Path
    ) -> HeadlessExecutionCapabilities:
        try:
            return self.reader.load_capabilities(runtime_worktree)
        except RuntimeContractError as exc:
            raise RuntimeVariantContractError(str(exc)) from exc
