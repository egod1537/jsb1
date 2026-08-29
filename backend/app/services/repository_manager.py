from __future__ import annotations

import os
import subprocess
import threading
import re
from pathlib import Path, PurePosixPath

from app.domain.repository import (
    Branch,
    Repository,
    RepositoryCreate,
    RepositoryStatus,
    Revision,
)
from app.repositories.jsb_repository_repository import JsbRepositoryRepository


class InvalidRepositoryPath(ValueError):
    pass


class GitOperationError(RuntimeError):
    pass


class RuntimeRepositoryNotConfigured(RuntimeError):
    pass


class RepositoryManager:
    def __init__(
        self,
        repository: JsbRepositoryRepository,
        repository_root: Path,
        worktree_root: Path,
    ) -> None:
        self.repository = repository
        self.repository_root = repository_root.resolve()
        self.worktree_root = worktree_root.resolve()
        self.repository_root.mkdir(parents=True, exist_ok=True)
        self.worktree_root.mkdir(parents=True, exist_ok=True)
        self._locks_guard = threading.Lock()
        self._locks: dict[int, threading.Lock] = {}

    def register(self, request: RepositoryCreate) -> RepositoryStatus:
        source = self._source_path(request.local_path)
        if source.exists():
            if not source.is_dir():
                raise InvalidRepositoryPath("repository local path is not a directory")
            self._verify_repository(source)
        else:
            self._validate_remote_url(request.remote_url)
            source.parent.mkdir(parents=True, exist_ok=True)
            self._run(
                ["git", "clone", "--origin", "origin", "--", request.remote_url, str(source)],
                operation="clone",
                cwd=self.repository_root,
                timeout=300,
            )
        current_branch = self._optional_git(source, ["branch", "--show-current"]) or None
        default_branch = request.default_branch or current_branch or "main"
        record = self.repository.create(
            name=request.name,
            remote_url=request.remote_url,
            local_path=PurePosixPath(request.local_path).as_posix(),
            default_branch=default_branch,
        )
        return self.status(record.id)

    def delete(self, repository_id: int) -> None:
        # Metadata deletion never removes a source checkout or experiment artifact.
        self.repository.delete(repository_id)

    def status(self, repository_id: int) -> RepositoryStatus:
        record = self.repository.get(repository_id)
        source = self.path_for(record)
        self._verify_repository(source)
        head = self._git(source, ["rev-parse", "HEAD"], operation="resolve HEAD")
        branch = self._optional_git(source, ["branch", "--show-current"]) or None
        dirty = bool(self._git(source, ["status", "--porcelain"], operation="status"))
        return RepositoryStatus(
            **record.model_dump(),
            current_branch=branch,
            head_commit=head,
            dirty=dirty,
        )

    def list_status(self) -> list[RepositoryStatus]:
        result: list[RepositoryStatus] = []
        for record in self.repository.list():
            try:
                result.append(self.status(record.id))
            except (GitOperationError, InvalidRepositoryPath):
                result.append(
                    RepositoryStatus(
                        **record.model_dump(),
                        current_branch=None,
                        head_commit="",
                        dirty=False,
                        status="error",
                    )
                )
        return result

    def runtime_repository(self, configured_name: str) -> RepositoryStatus:
        """Return the single platform-configured JSB0 Runtime repository."""
        try:
            record = self.repository.get_by_name(configured_name)
        except KeyError as exc:
            raise RuntimeRepositoryNotConfigured(
                "JSB0 Runtime repository is not configured"
            ) from exc
        return self.status(record.id)

    def fetch(self, repository_id: int) -> RepositoryStatus:
        record = self.repository.get(repository_id)
        source = self.path_for(record)
        self._git(source, ["fetch", "--all", "--prune"], operation="fetch", timeout=300)
        self.repository.mark_fetched(repository_id)
        return self.status(repository_id)

    def branches(self, repository_id: int) -> list[Branch]:
        record = self.repository.get(repository_id)
        source = self.path_for(record)
        current = self._optional_git(source, ["branch", "--show-current"])
        output = self._git(
            source,
            [
                "for-each-ref",
                "--format=%(refname)|%(objectname)",
                "refs/heads",
                "refs/remotes/origin",
            ],
            operation="list branches",
        )
        branches: list[Branch] = []
        seen: set[tuple[str, bool]] = set()
        for line in output.splitlines():
            ref, separator, sha = line.partition("|")
            if not separator:
                continue
            remote = ref.startswith("refs/remotes/origin/")
            prefix = "refs/remotes/origin/" if remote else "refs/heads/"
            name = ref.removeprefix(prefix)
            if name == "HEAD" or (name, remote) in seen:
                continue
            seen.add((name, remote))
            branches.append(
                Branch(name=name, commit_sha=sha, current=not remote and name == current, remote=remote)
            )
        return sorted(branches, key=lambda item: (item.remote, item.name.lower()))

    def revision(self, repository_id: int, revision: str) -> Revision:
        if "\x00" in revision or revision.startswith("-"):
            raise GitOperationError("invalid revision")
        record = self.repository.get(repository_id)
        source = self.path_for(record)
        commit_sha = self._git(
            source,
            ["rev-parse", "--verify", "--end-of-options", f"{revision}^{{commit}}"],
            operation="resolve revision",
        )
        metadata = self._git(
            source,
            ["show", "-s", "--format=%H%x00%s%x00%cI", commit_sha],
            operation="read commit metadata",
        )
        parts = metadata.split("\x00", 2)
        if len(parts) != 3:
            raise GitOperationError("git returned invalid commit metadata")
        branch_names = {
            item.name for item in self.branches(repository_id) if item.commit_sha == commit_sha
        }
        branch = revision if revision in branch_names else next(iter(sorted(branch_names)), None)
        dirty = bool(self._git(source, ["status", "--porcelain"], operation="status"))
        return Revision(
            repository_id=repository_id,
            commit_sha=parts[0],
            branch=branch,
            commit_message=parts[1],
            committed_at=parts[2],
            dirty=dirty,
        )

    def resolve_branch(self, repository_id: int, branch: str) -> Revision:
        """Resolve a moving branch, preferring the freshly fetched origin ref."""
        self.validate_branch_name(branch)
        record = self.repository.get(repository_id)
        source = self.path_for(record)
        refs = (f"refs/remotes/origin/{branch}", f"refs/heads/{branch}")
        for ref in refs:
            commit_sha = self._optional_git(
                source,
                ["rev-parse", "--verify", "--end-of-options", f"{ref}^{{commit}}"],
            )
            if commit_sha:
                revision = self.revision(repository_id, commit_sha)
                return revision.model_copy(update={"branch": branch})
        raise GitOperationError(f"branch not found: {branch}")

    def prepare_worktree(self, repository_id: int, commit_sha: str) -> Path:
        record = self.repository.get(repository_id)
        source = self.path_for(record)
        resolved = self.revision(repository_id, commit_sha).commit_sha
        destination = self._under(
            self.worktree_root, self.worktree_root / str(repository_id) / resolved
        )
        lock = self._lock_for(repository_id)
        with lock:
            if destination.exists():
                existing = self._git(
                    destination, ["rev-parse", "HEAD"], operation="inspect worktree"
                )
                if existing != resolved:
                    raise GitOperationError("existing worktree points to a different commit")
                return destination
            destination.parent.mkdir(parents=True, exist_ok=True)
            self._git(source, ["worktree", "prune"], operation="prune worktrees")
            self._git(
                source,
                ["worktree", "add", "--detach", str(destination), resolved],
                operation="create worktree",
                timeout=300,
            )
        return destination

    def path_for(self, repository: Repository) -> Path:
        return self._source_path(repository.local_path)

    def _source_path(self, relative: str) -> Path:
        candidate = PurePosixPath(relative)
        if candidate.is_absolute() or not candidate.parts or ".." in candidate.parts:
            raise InvalidRepositoryPath("local_path must be relative to repository root")
        return self._under(self.repository_root, self.repository_root.joinpath(*candidate.parts))

    @staticmethod
    def _under(root: Path, candidate: Path) -> Path:
        resolved = candidate.resolve()
        try:
            resolved.relative_to(root.resolve())
        except ValueError as exc:
            raise InvalidRepositoryPath("path escapes configured root") from exc
        return resolved

    def _verify_repository(self, source: Path) -> None:
        top = self._git(source, ["rev-parse", "--show-toplevel"], operation="validate repository")
        if Path(top).resolve() != source.resolve():
            raise InvalidRepositoryPath("local_path must point to a repository root")

    @staticmethod
    def _validate_remote_url(value: str) -> None:
        safe_scheme = value.startswith(("https://", "http://", "ssh://", "git://"))
        safe_scp = re.fullmatch(
            r"[A-Za-z0-9._-]+@[A-Za-z0-9.-]+:[A-Za-z0-9._/~+-]+(?:/[A-Za-z0-9._~+-]+)*",
            value,
        )
        if not safe_scheme and safe_scp is None:
            raise GitOperationError("remote_url must use http(s), ssh, git, or scp syntax")

    @staticmethod
    def validate_branch_name(value: str) -> None:
        if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._/-]{0,254}", value):
            raise GitOperationError("invalid branch name")
        if (
            value.endswith(("/", ".", ".lock"))
            or ".." in value
            or "//" in value
            or "@{" in value
            or "/." in value
        ):
            raise GitOperationError("invalid branch name")

    def _git(
        self,
        source: Path,
        args: list[str],
        *,
        operation: str,
        timeout: float = 60,
    ) -> str:
        return self._run(
            ["git", "-C", str(source), *args], operation=operation, timeout=timeout
        )

    def _optional_git(self, source: Path, args: list[str]) -> str:
        try:
            return self._git(source, args, operation="read repository state")
        except GitOperationError:
            return ""

    @staticmethod
    def _run(
        command: list[str],
        *,
        operation: str,
        cwd: Path | None = None,
        timeout: float,
    ) -> str:
        environment = os.environ.copy()
        environment["GIT_TERMINAL_PROMPT"] = "0"
        try:
            result = subprocess.run(
                command,
                cwd=cwd,
                env=environment,
                check=False,
                capture_output=True,
                text=True,
                timeout=timeout,
            )
        except (OSError, subprocess.TimeoutExpired) as exc:
            raise GitOperationError(f"git {operation} could not be completed") from exc
        if result.returncode != 0:
            detail = result.stderr.strip().splitlines()
            suffix = f": {detail[-1][:500]}" if detail else ""
            raise GitOperationError(f"git {operation} failed{suffix}")
        return result.stdout.strip()

    def _lock_for(self, repository_id: int) -> threading.Lock:
        with self._locks_guard:
            return self._locks.setdefault(repository_id, threading.Lock())
