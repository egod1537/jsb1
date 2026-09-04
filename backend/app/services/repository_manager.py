from __future__ import annotations

import threading
from pathlib import Path

from app.domain.errors import RepositoryConflict, RuntimeRevisionNotFound
from app.domain.repository import (
    Branch,
    Repository,
    RepositoryCreate,
    RepositoryStatus,
    Revision,
    RuntimeRepositoryStatus,
)
from app.infrastructure.git import (
    GitOperationError,
    GitReferencePolicy,
    GitRepositoryAdapter,
    InvalidRepositoryFilesystemPath,
    RepositoryPathResolver,
    WorktreeManager,
)
from app.repositories.jsb_repository_repository import JsbRepositoryRepository


class InvalidRepositoryPath(ValueError):
    pass


class RuntimeRepositoryNotConfigured(RuntimeError):
    pass


class RuntimeRepositoryUnavailable(RuntimeError):
    pass


RUNTIME_REPOSITORY_KEY = "jsb0"


class RepositoryManager:
    def __init__(
        self,
        repository: JsbRepositoryRepository,
        repository_root: Path,
        worktree_root: Path,
        git: GitRepositoryAdapter | None = None,
    ) -> None:
        self.repository = repository
        self.paths = RepositoryPathResolver(repository_root, worktree_root)
        self.repository_root = self.paths.repository_root
        self.worktree_root = self.paths.worktree_root
        self.git = git or GitRepositoryAdapter()
        self.worktrees = WorktreeManager(self.worktree_root, self.git)
        self._runtime_repository_id: int | None = None
        self._runtime_error: str | None = None
        self._runtime_warning: str | None = None
        self._fetch_locks_guard = threading.Lock()
        self._fetch_locks: dict[int, threading.Lock] = {}

    def ensure_runtime_repository(
        self, remote_url: str, source_path: Path, default_branch: str
    ) -> RuntimeRepositoryStatus:
        """Bootstrap or reconcile the one operator-configured JSB0 Runtime clone."""
        normalized_url = self.normalize_remote_url(remote_url)
        self._validate_remote_url(normalized_url)
        self.validate_branch_name(default_branch)
        resolved_source = source_path.expanduser().resolve()
        if resolved_source in {Path("/"), self.repository_root}:
            raise InvalidRepositoryPath(
                "JSB0_REPOSITORY_PATH must identify a dedicated repository directory"
            )
        stored_path = self._stored_path(resolved_source)
        record = self._matching_runtime_record(normalized_url, resolved_source)
        same_existing_path = False
        if record is not None:
            try:
                same_existing_path = self.path_for(record) == resolved_source
            except InvalidRepositoryPath:
                pass
        if record is None:
            record = self.repository.create(
                name=RUNTIME_REPOSITORY_KEY,
                remote_url=normalized_url,
                local_path=stored_path,
                default_branch=default_branch,
            )
        elif (
            record.remote_url != normalized_url
            or record.local_path != stored_path
            or record.default_branch != default_branch
        ):
            record = self.repository.update_configuration(
                record.id,
                remote_url=normalized_url,
                local_path=stored_path,
                default_branch=default_branch,
            )
        self._runtime_repository_id = record.id
        self._runtime_error = None
        self._runtime_warning = None
        try:
            clone_required = not resolved_source.exists()
            if resolved_source.exists():
                if not resolved_source.is_dir():
                    raise InvalidRepositoryPath(
                        "JSB0_REPOSITORY_PATH is not a directory"
                    )
                clone_required = not any(resolved_source.iterdir())
                if not clone_required:
                    self._verify_repository(resolved_source)
            if clone_required:
                self.git.clone(
                    normalized_url,
                    resolved_source,
                    operation="clone JSB0 Runtime repository",
                    timeout=300,
                    runner=self._run,
                )
            origin = self._optional_git(
                resolved_source, ["remote", "get-url", "origin"]
            )
            if not origin:
                self._git(
                    resolved_source,
                    ["remote", "add", "origin", normalized_url],
                    operation="configure JSB0 origin",
                )
            elif self.normalize_remote_url(origin) != normalized_url:
                if not same_existing_path:
                    raise InvalidRepositoryPath(
                        "existing repository origin does not match JSB0_REPOSITORY_URL"
                    )
                self._git(
                    resolved_source,
                    ["remote", "set-url", "origin", normalized_url],
                    operation="update JSB0 origin",
                )
            self._refresh_default_branch_warning(record)
        except (GitOperationError, InvalidRepositoryPath, OSError) as exc:
            self._runtime_error = str(exc)
        return self.runtime_status()

    def record_runtime_configuration_error(self, error: Exception) -> None:
        """Keep startup configuration failures visible through the runtime API."""
        self._runtime_error = str(error)

    def runtime_status(self) -> RuntimeRepositoryStatus:
        record = self._runtime_record()
        error = self._runtime_error
        try:
            status = self.status(record.id)
            if error is None:
                return self._runtime_public_status(status, self._runtime_warning)
        except (GitOperationError, InvalidRepositoryPath, OSError) as exc:
            error = error or str(exc)
        return RuntimeRepositoryStatus(
            id=record.id,
            display_name=self._display_name(record.remote_url),
            remote_url=record.remote_url,
            local_path=str(self.path_for(record)),
            default_branch=record.default_branch,
            last_fetched_at=record.last_fetched_at,
            status="error",
            error=error or "JSB0 Runtime repository is unavailable",
        )

    def runtime_repository(self) -> RepositoryStatus:
        record = self._runtime_record()
        try:
            status = self.status(record.id)
        except (GitOperationError, InvalidRepositoryPath, OSError) as exc:
            self._runtime_error = str(exc)
            raise RuntimeRepositoryUnavailable(
                "JSB0 Runtime repository is unavailable"
            ) from exc
        self._runtime_error = None
        return status

    def fetch_runtime_repository(self) -> RuntimeRepositoryStatus:
        record = self._runtime_record()
        try:
            self.fetch(record.id)
            self._runtime_error = None
            self._refresh_default_branch_warning(record)
        except (KeyError, GitOperationError, InvalidRepositoryPath, OSError) as exc:
            self._runtime_error = str(exc)
            raise RuntimeRepositoryUnavailable(
                "Could not fetch JSB0 Runtime repository"
            ) from exc
        return self.runtime_status()

    def runtime_branches(self) -> list[Branch]:
        repository = self.runtime_repository()
        branches: dict[str, Branch] = {}
        for branch in self.branches(repository.id):
            if branch.remote or branch.name not in branches:
                branches[branch.name] = branch
        return sorted(branches.values(), key=lambda item: item.name.lower())

    def register(self, request: RepositoryCreate) -> RepositoryStatus:
        source = self._source_path(request.local_path)
        if source.exists():
            if not source.is_dir():
                raise InvalidRepositoryPath("repository local path is not a directory")
            self._verify_repository(source)
        else:
            self._validate_remote_url(request.remote_url)
            self.git.clone(
                request.remote_url,
                source,
                operation="clone",
                timeout=300,
                runner=self._run,
            )
        current_branch = (
            self._optional_git(source, ["branch", "--show-current"]) or None
        )
        default_branch = request.default_branch or current_branch or "main"
        record = self.repository.create(
            name=request.name,
            remote_url=request.remote_url,
            local_path=self._stored_path(source),
            default_branch=default_branch,
        )
        return self.status(record.id)

    def delete(self, repository_id: int) -> None:
        # Metadata deletion never removes a source checkout or experiment artifact.
        record = self.repository.get(repository_id)
        if (
            record.name.lower() == RUNTIME_REPOSITORY_KEY
            or repository_id == self._runtime_repository_id
        ):
            raise RepositoryConflict(
                "the configured JSB0 Runtime repository cannot be deleted"
            )
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

    def fetch(self, repository_id: int) -> RepositoryStatus:
        with self._fetch_lock_for(repository_id):
            record = self.repository.get(repository_id)
            source = self.path_for(record)
            origin = self._optional_git(source, ["remote", "get-url", "origin"])
            arguments = (
                [
                    "fetch",
                    "--prune",
                    "origin",
                    "+refs/heads/*:refs/remotes/origin/*",
                ]
                if origin
                else ["fetch", "--all", "--prune"]
            )
            self._git(source, arguments, operation="fetch", timeout=300)
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
                Branch(
                    name=name,
                    commit_sha=sha,
                    current=not remote and name == current,
                    remote=remote,
                )
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
            item.name
            for item in self.branches(repository_id)
            if item.commit_sha == commit_sha
        }
        branch = (
            revision
            if revision in branch_names
            else next(iter(sorted(branch_names)), None)
        )
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
        raise RuntimeRevisionNotFound(f"branch not found: {branch}")

    def prepare_worktree(self, repository_id: int, commit_sha: str) -> Path:
        record = self.repository.get(repository_id)
        source = self.path_for(record)
        resolved = self.revision(repository_id, commit_sha).commit_sha
        return self.worktrees.prepare(repository_id, source, resolved)

    def path_for(self, repository: Repository) -> Path:
        return self._source_path(repository.local_path)

    def _source_path(self, configured: str) -> Path:
        try:
            return self.paths.source(configured)
        except InvalidRepositoryFilesystemPath as exc:
            raise InvalidRepositoryPath(str(exc)) from exc

    def _stored_path(self, source: Path) -> str:
        return self.paths.stored(source)

    def _matching_runtime_record(
        self, normalized_url: str, source_path: Path
    ) -> Repository | None:
        records = self.repository.list()
        named = next(
            (
                record
                for record in records
                if record.name.lower() == RUNTIME_REPOSITORY_KEY
            ),
            None,
        )
        if named is not None:
            return named
        for record in records:
            try:
                same_path = self.path_for(record) == source_path
            except InvalidRepositoryPath:
                same_path = False
            if (
                same_path
                or self.normalize_remote_url(record.remote_url) == normalized_url
            ):
                return record
        return None

    def _fetch_lock_for(self, repository_id: int) -> threading.Lock:
        with self._fetch_locks_guard:
            return self._fetch_locks.setdefault(repository_id, threading.Lock())

    def _runtime_record(self) -> Repository:
        if self._runtime_repository_id is not None:
            try:
                return self.repository.get(self._runtime_repository_id)
            except KeyError:
                self._runtime_repository_id = None
        try:
            record = self.repository.get_by_name(RUNTIME_REPOSITORY_KEY)
        except KeyError as exc:
            if self._runtime_error:
                raise RuntimeRepositoryUnavailable(self._runtime_error) from exc
            raise RuntimeRepositoryNotConfigured(
                "JSB0 Runtime repository is not configured"
            ) from exc
        self._runtime_repository_id = record.id
        return record

    def _refresh_default_branch_warning(self, record: Repository) -> None:
        self._runtime_warning = None
        if record.default_branch not in {
            branch.name for branch in self.branches(record.id)
        }:
            self._runtime_warning = (
                f"Configured default branch is unavailable: {record.default_branch}"
            )

    def _runtime_public_status(
        self, status: RepositoryStatus, error: str | None
    ) -> RuntimeRepositoryStatus:
        return RuntimeRepositoryStatus(
            id=status.id,
            display_name=self._display_name(status.remote_url),
            remote_url=status.remote_url,
            local_path=str(self.path_for(status)),
            default_branch=status.default_branch,
            last_fetched_at=status.last_fetched_at,
            current_branch=status.current_branch,
            head_commit=status.head_commit,
            dirty=status.dirty,
            status="error"
            if self._runtime_error
            else "warning"
            if error
            else status.status,
            error=error,
        )

    @staticmethod
    def normalize_remote_url(value: str) -> str:
        return GitReferencePolicy.normalize_remote_url(value)

    @classmethod
    def _display_name(cls, remote_url: str) -> str:
        return GitReferencePolicy.display_name(
            remote_url, fallback=RUNTIME_REPOSITORY_KEY
        )

    @staticmethod
    def _under(root: Path, candidate: Path) -> Path:
        try:
            return RepositoryPathResolver.under(root, candidate)
        except InvalidRepositoryFilesystemPath as exc:
            raise InvalidRepositoryPath(str(exc)) from exc

    def _verify_repository(self, source: Path) -> None:
        top = self._git(
            source, ["rev-parse", "--show-toplevel"], operation="validate repository"
        )
        if Path(top).resolve() != source.resolve():
            raise InvalidRepositoryPath("local_path must point to a repository root")

    @staticmethod
    def _validate_remote_url(value: str) -> None:
        GitReferencePolicy.validate_remote_url(value)

    @staticmethod
    def validate_branch_name(value: str) -> None:
        GitReferencePolicy.validate_branch_name(value)

    def _git(
        self,
        source: Path,
        args: list[str],
        *,
        operation: str,
        timeout: float = 60,
    ) -> str:
        return self.git.git(source, args, operation=operation, timeout=timeout)

    def _optional_git(self, source: Path, args: list[str]) -> str:
        return self.git.optional(source, args)

    def _run(
        self,
        command: list[str],
        *,
        operation: str,
        cwd: Path | None = None,
        timeout: float,
    ) -> str:
        return self.git.run(command, operation=operation, cwd=cwd, timeout=timeout)
