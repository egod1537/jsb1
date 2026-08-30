from __future__ import annotations

import io
import json
import stat
from pathlib import Path
from types import SimpleNamespace
from typing import ClassVar

import paramiko
from app.config.settings import Settings
from app.repositories.database import Database
from app.repositories.scenario_catalog import ScenarioCatalogRepository
from app.services.scenario_sources.base import ScenarioObject
from app.services.scenario_sources.sftp import SftpScenarioSource
from app.services.scenario_sync import (
    CompatibilityContract,
    ScenarioSyncService,
)
from app.services.scenario_validator import ScenarioRuntime, ScenarioValidator


def write_contract(root: Path) -> None:
    path = root / "contract" / "scenario"
    path.mkdir(parents=True)
    (path / "scenario.schema.json").write_text(
        json.dumps(
            {
                "$schema": "https://json-schema.org/draft/2020-12/schema",
                "type": "object",
                "required": ["name", "autopilot"],
                "properties": {
                    "name": {"type": "string"},
                    "schema_version": {"type": "integer"},
                    "autopilot": {"enum": ["baseline", "primary"]},
                    "controller_parameters": {
                        "type": "array",
                        "items": {"type": "string"},
                        "uniqueItems": True,
                    },
                },
                "additionalProperties": False,
            }
        ),
        encoding="utf-8",
    )


def test_validator_returns_structured_multiple_errors(tmp_path: Path) -> None:
    runtime = tmp_path / "runtime"
    write_contract(runtime)
    validator = ScenarioValidator()
    contract = validator.load_runtime_contract(runtime)

    result = validator.validate_yaml(
        "schema_version: bad\nautopilot: experimental\nunexpected: true\n",
        contract,
        runtime_branch="main",
        runtime_commit="a" * 40,
    )

    assert not result.valid
    assert result.runtime is not None and result.runtime.commit == "a" * 40
    assert {(error.path, error.code) for error in result.errors} >= {
        ("name", "required"),
        ("autopilot", "enum"),
        ("schema_version", "type"),
        ("$", "additionalProperties"),
    }


def test_validator_rejects_controller_parameter_missing_from_runtime_contract(
    tmp_path: Path,
) -> None:
    runtime = tmp_path / "runtime"
    write_contract(runtime)
    validator = ScenarioValidator()
    contract = validator.load_runtime_contract(runtime)

    result = validator.validate_yaml(
        "name: Roll Hold\nautopilot: baseline\n"
        "controller_parameters:\n  - FW_RR_P\n  - UNKNOWN_GAIN\n",
        contract,
        runtime_branch="main",
        runtime_commit="a" * 40,
    )

    assert not result.valid
    assert [error.model_dump() for error in result.errors] == [{
        "path": "controller_parameters.1",
        "code": "unsupported_controller_parameter",
        "message": (
            "Unsupported controller parameter for selected JSB0 revision: "
            "UNKNOWN_GAIN"
        ),
    }]


def test_empty_compose_sftp_values_do_not_become_paths(tmp_path: Path) -> None:
    settings = Settings(
        _env_file=None,
        data_dir=tmp_path,
        scenario_sftp_host="",
        scenario_sftp_user="",
        scenario_sftp_key_path="",
        scenario_sftp_known_hosts_path="",
    )
    assert settings.scenario_sftp_host is None
    assert settings.scenario_sftp_user is None
    assert settings.scenario_sftp_key_path is None
    assert settings.scenario_sftp_known_hosts_path is None


class FakeSftp:
    def __init__(self) -> None:
        self.files = {"/library/roll/foo.yaml": b"name: Foo\nautopilot: baseline\n"}

    def listdir_attr(self, path: str):
        if path == "/library":
            return [SimpleNamespace(filename="roll", st_mode=stat.S_IFDIR, st_size=0, st_mtime=1)]
        if path == "/library/roll":
            return [SimpleNamespace(filename="foo.yaml", st_mode=stat.S_IFREG, st_size=30, st_mtime=2)]
        return []

    def open(self, path: str, mode: str):
        assert mode == "rb"
        return io.BytesIO(self.files[path])

    def stat(self, path: str):
        return SimpleNamespace(st_size=len(self.files[path]), st_mtime=2)

    def close(self) -> None:
        pass


class FakeSshClient:
    instances: ClassVar[list[FakeSshClient]] = []

    def __init__(self) -> None:
        self.sftp = FakeSftp()
        self.policy = None
        self.connect_args = {}
        self.instances.append(self)

    def load_system_host_keys(self, filename=None) -> None:
        self.known_hosts = filename

    def set_missing_host_key_policy(self, policy) -> None:
        self.policy = policy

    def connect(self, **kwargs) -> None:
        self.connect_args = kwargs

    def open_sftp(self):
        return self.sftp

    def close(self) -> None:
        pass


def test_sftp_source_recurses_and_enforces_root(tmp_path: Path) -> None:
    FakeSshClient.instances.clear()
    source = SftpScenarioSource(
        host="scenario.example.test",
        port=22,
        username="operator",
        root="/library",
        key_path=tmp_path / "id_ed25519",
        known_hosts_path=tmp_path / "known_hosts",
        client_factory=FakeSshClient,
    )

    assert [item.id for item in source.list_objects()] == ["roll/foo.yaml"]
    assert source.fetch("roll/foo.yaml").startswith(b"name: Foo")
    assert source.stat("roll/foo.yaml").size
    client = FakeSshClient.instances[-1]
    assert isinstance(client.policy, paramiko.RejectPolicy)
    assert client.connect_args["allow_agent"] is True
    assert client.connect_args["look_for_keys"] is False
    try:
        source.fetch("../secret.yaml")
    except ValueError as exc:
        assert "relative YAML" in str(exc)
    else:
        raise AssertionError("path traversal was accepted")


def test_sftp_auth_or_host_key_failure_is_not_silenced() -> None:
    class RejectingClient(FakeSshClient):
        def connect(self, **kwargs) -> None:
            raise paramiko.SSHException("host key rejected")

    source = SftpScenarioSource(
        host="scenario.example.test",
        port=22,
        username="operator",
        root="/library",
        client_factory=RejectingClient,
    )
    try:
        source.list_objects()
    except paramiko.SSHException as exc:
        assert "host key rejected" in str(exc)
    else:
        raise AssertionError("SSH failure was ignored")


def test_sync_error_messages_distinguish_ssh_failure_classes() -> None:
    assert (
        ScenarioSyncService._public_error(paramiko.AuthenticationException())
        == "SFTP authentication failed"
    )
    assert (
        ScenarioSyncService._public_error(paramiko.SSHException("not found in known_hosts"))
        == "SFTP host key verification failed"
    )
    assert (
        ScenarioSyncService._public_error(ConnectionError("offline"))
        == "SFTP connection or file read failed"
    )


class FakeSource:
    def __init__(self, files: dict[str, bytes]) -> None:
        self.files = files
        self.failure: Exception | None = None

    def list_objects(self):
        if self.failure:
            raise self.failure
        return [ScenarioObject(name, len(value)) for name, value in self.files.items()]

    def fetch(self, object_id: str) -> bytes:
        return self.files[object_id]

    def stat(self, object_id: str):
        return ScenarioObject(object_id, len(self.files[object_id]))


class FakeCompatibility:
    def __init__(self, root: Path) -> None:
        self.value = CompatibilityContract(root, ScenarioRuntime(branch="main", commit="b" * 40))

    def resolve(self):
        return self.value


def test_sync_preserves_last_good_cache_and_survives_outage(tmp_path: Path) -> None:
    runtime = tmp_path / "runtime"
    write_contract(runtime)
    migrations = Path(__file__).resolve().parents[1] / "migrations"
    database = Database(tmp_path / "data" / "jsb1.db", migrations)
    database.initialize()
    catalog = ScenarioCatalogRepository(database)
    source = FakeSource({"roll/foo.yaml": b"name: Foo\nautopilot: baseline\n"})
    cache = tmp_path / "data" / "scenarios" / "remote"
    service = ScenarioSyncService(
        source=source,
        cache_root=cache,
        catalog=catalog,
        validator=ScenarioValidator(),
        compatibility=FakeCompatibility(runtime),  # type: ignore[arg-type]
    )

    first = service.sync()
    cached = cache / "roll" / "foo.yaml"
    assert first.valid == 1 and first.updated == 1
    assert cached.read_bytes() == source.files["roll/foo.yaml"]

    source.files["roll/foo.yaml"] = b"name: Broken\nautopilot: unsupported\n"
    rejected = service.sync()
    assert rejected.invalid == 1
    assert cached.read_text(encoding="utf-8").endswith("baseline\n")
    assert catalog.list_valid()[0]["name"] == "Foo"
    assert catalog.list_invalid()[0]["errors"][0]["path"] == "autopilot"

    source.files["roll/foo.yaml"] = b"name: Updated\nautopilot: primary\n"
    replaced = service.sync()
    assert replaced.valid == 1 and replaced.updated == 1
    assert cached.read_text(encoding="utf-8").startswith("name: Updated")
    assert catalog.list_valid()[0]["name"] == "Updated"

    source.failure = ConnectionError("offline")
    offline = service.sync()
    assert not offline.reachable
    assert cached.is_file()
    assert catalog.list_valid()[0]["scenario_id"] == "roll/foo.yaml"


def test_successful_sync_marks_remote_deletion_inactive(tmp_path: Path) -> None:
    runtime = tmp_path / "runtime"
    write_contract(runtime)
    database = Database(
        tmp_path / "data" / "jsb1.db",
        Path(__file__).resolve().parents[1] / "migrations",
    )
    database.initialize()
    source = FakeSource({"foo.yaml": b"name: Foo\nautopilot: primary\n"})
    catalog = ScenarioCatalogRepository(database)
    service = ScenarioSyncService(
        source=source,
        cache_root=tmp_path / "data" / "scenarios" / "remote",
        catalog=catalog,
        validator=ScenarioValidator(),
        compatibility=FakeCompatibility(runtime),  # type: ignore[arg-type]
    )
    service.sync()
    source.files.clear()

    result = service.sync()

    assert result.removed == 1
    assert catalog.list_valid() == []
