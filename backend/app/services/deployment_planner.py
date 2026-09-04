from __future__ import annotations

import hashlib
import re
from collections.abc import Callable
from dataclasses import dataclass

from app.config.settings import Settings
from app.domain.deployment import BranchDeployment
from app.domain.errors import DeploymentOperationError


def branch_slug(branch: str, *, max_length: int = 48) -> str:
    """Return a conservative DNS label without assigning ownership."""
    value = re.sub(r"[^a-z0-9-]+", "-", branch.lower())
    value = re.sub(r"-+", "-", value).strip("-")
    value = value[:max_length].rstrip("-")
    if not value:
        raise DeploymentOperationError(
            "branch does not produce a valid deployment slug"
        )
    return value


def deployment_hostname(
    slug: str,
    *,
    branch: str,
    base_domain: str,
    main_branch: str = "main",
    main_hostname: str | None = None,
) -> str:
    if branch == main_branch:
        return main_hostname or f"jsb.{base_domain}"
    return f"{slug}-jsb.{base_domain}"


@dataclass(frozen=True)
class DeploymentPlan:
    repository_id: int
    branch: str
    commit_sha: str
    slug: str
    hostname: str


SlugOwnerLookup = Callable[..., BranchDeployment | None]


class DeploymentPlanner:
    """Pure deployment naming policy; it performs no Git, DB, or process I/O."""

    def __init__(self, settings: Settings) -> None:
        self.settings = settings

    def plan(
        self,
        *,
        repository_id: int,
        branch: str,
        commit_sha: str,
        previous: BranchDeployment | None,
        slug_owner: SlugOwnerLookup,
    ) -> DeploymentPlan:
        slug = self.slug_for(
            repository_id=repository_id,
            branch=branch,
            commit_sha=commit_sha,
            previous=previous,
            slug_owner=slug_owner,
        )
        return DeploymentPlan(
            repository_id=repository_id,
            branch=branch,
            commit_sha=commit_sha,
            slug=slug,
            hostname=deployment_hostname(
                slug,
                branch=branch,
                base_domain=self.settings.deployment_base_domain,
                main_branch=self.settings.deployment_main_branch,
                main_hostname=self.settings.deployment_main_hostname,
            ),
        )

    def slug_for(
        self,
        *,
        repository_id: int,
        branch: str,
        commit_sha: str,
        previous: BranchDeployment | None,
        slug_owner: SlugOwnerLookup,
    ) -> str:
        if branch == self.settings.deployment_main_branch:
            return "main"
        if previous is not None:
            return previous.slug
        base = branch_slug(branch)
        main_suffix = f".{self.settings.deployment_base_domain}"
        main_label = self.settings.deployment_main_hostname.removesuffix(main_suffix)
        owner = slug_owner(base, repository_id=repository_id, branch=branch)
        if owner is None and base not in {"main", main_label}:
            return base
        candidate = f"{base[:55].rstrip('-')}-{commit_sha[:7].lower()}"
        owner = slug_owner(candidate, repository_id=repository_id, branch=branch)
        if owner is None:
            return candidate
        digest = hashlib.sha256(branch.encode("utf-8")).hexdigest()[:6]
        return f"{base[:48].rstrip('-')}-{commit_sha[:7].lower()}-{digest}"
