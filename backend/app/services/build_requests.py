from __future__ import annotations

import asyncio
from pathlib import Path

from app.domain.build import Build
from app.services.build_manager import BuildManager
from app.services.ports import BuildDispatcher


class BuildRequestService:
    """Application use cases for querying and scheduling immutable builds."""

    def __init__(self, manager: BuildManager, scheduler: BuildDispatcher) -> None:
        self.manager = manager
        self.scheduler = scheduler

    def list(self, *, repository_id: int | None, limit: int) -> list[Build]:
        return [
            self.manager.public(build)
            for build in self.manager.list(repository_id=repository_id, limit=limit)
        ]

    def get(self, build_id: int) -> Build:
        return self.manager.public(self.manager.get(build_id))

    async def request(
        self, repository_id: int, revision: str, *, rebuild: bool = False
    ) -> Build:
        build, reused = await asyncio.to_thread(
            self.manager.request,
            repository_id,
            revision,
            rebuild=rebuild,
        )
        if not reused:
            self.scheduler.submit(build.id)
        return self.manager.public(build)

    async def rebuild(self, build_id: int) -> Build:
        previous = self.get(build_id)
        return await self.request(
            previous.repository_id,
            previous.commit_sha,
            rebuild=True,
        )

    def log_path(self, build_id: int, stream: str) -> Path:
        return self.manager.log_path(build_id, stream)
