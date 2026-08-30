from __future__ import annotations

import posixpath
import stat as stat_module
from collections.abc import Iterator
from contextlib import contextmanager
from pathlib import Path, PurePosixPath
from typing import Any

import paramiko

from app.services.scenario_sources.base import ScenarioObject, validate_object_id


class SftpScenarioSource:
    """Read-only SFTP source constrained to one configured remote root."""

    def __init__(
        self,
        *,
        host: str,
        port: int,
        username: str,
        root: str,
        key_path: Path | None = None,
        password: str | None = None,
        known_hosts_path: Path | None = None,
        timeout_sec: float = 15.0,
        client_factory: Any = paramiko.SSHClient,
    ) -> None:
        if not host.strip() or not username.strip():
            raise ValueError("SFTP host and user are required")
        if not root.startswith("/") or "\x00" in root:
            raise ValueError("SFTP root must be an absolute POSIX path")
        self.host = host
        self.port = port
        self.username = username
        self.root = posixpath.normpath(root)
        self.key_path = key_path
        self.password = password
        self.known_hosts_path = known_hosts_path
        self.timeout_sec = timeout_sec
        self.client_factory = client_factory

    def list_objects(self) -> list[ScenarioObject]:
        with self._session() as sftp:
            result: list[ScenarioObject] = []
            self._walk(sftp, self.root, PurePosixPath(), result)
            return sorted(result, key=lambda item: item.id)

    def fetch(self, object_id: str) -> bytes:
        remote = self._remote_path(object_id)
        with self._session() as sftp, sftp.open(remote, "rb") as stream:
            return stream.read()

    def stat(self, object_id: str) -> ScenarioObject:
        remote = self._remote_path(object_id)
        with self._session() as sftp:
            value = sftp.stat(remote)
        return ScenarioObject(object_id, value.st_size, value.st_mtime)

    def _walk(self, sftp: Any, remote: str, relative: PurePosixPath, output: list[ScenarioObject]) -> None:
        for item in sftp.listdir_attr(remote):
            if item.filename in {".", ".."} or "/" in item.filename or "\x00" in item.filename:
                continue
            child_remote = posixpath.join(remote, item.filename)
            child_relative = relative / item.filename
            if stat_module.S_ISDIR(item.st_mode):
                self._walk(sftp, child_remote, child_relative, output)
            elif stat_module.S_ISREG(item.st_mode) and child_relative.suffix.lower() in {".yaml", ".yml"}:
                scenario_id = validate_object_id(child_relative.as_posix()).as_posix()
                output.append(ScenarioObject(scenario_id, item.st_size, item.st_mtime))

    def _remote_path(self, object_id: str) -> str:
        relative = validate_object_id(object_id)
        candidate = posixpath.normpath(posixpath.join(self.root, relative.as_posix()))
        if posixpath.commonpath([self.root, candidate]) != self.root:
            raise ValueError("scenario object escapes SFTP root")
        return candidate

    @contextmanager
    def _session(self) -> Iterator[Any]:
        client = self.client_factory()
        client.load_system_host_keys(
            str(self.known_hosts_path) if self.known_hosts_path is not None else None
        )
        client.set_missing_host_key_policy(paramiko.RejectPolicy())
        try:
            client.connect(
                hostname=self.host,
                port=self.port,
                username=self.username,
                key_filename=str(self.key_path) if self.key_path is not None else None,
                password=self.password,
                allow_agent=True,
                look_for_keys=self.key_path is None,
                timeout=self.timeout_sec,
                banner_timeout=self.timeout_sec,
                auth_timeout=self.timeout_sec,
            )
            sftp = client.open_sftp()
            try:
                yield sftp
            finally:
                sftp.close()
        finally:
            client.close()
