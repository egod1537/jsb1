from __future__ import annotations

from fastapi.testclient import TestClient

from app.config.settings import Settings
from app.main import create_app
from app.services.build_info import load_build_info
from tests.conftest import FakeSimulationRunner


FULL_COMMIT = "8efb0664ab92f2df6155281415fbe33051366868"


def test_build_metadata_maps_deployment_environment(monkeypatch, tmp_path) -> None:
    monkeypatch.setenv("JSB1_DEPLOY_BRANCH", "impl")
    monkeypatch.setenv("JSB1_DEPLOY_COMMIT", FULL_COMMIT)
    monkeypatch.setenv("JSB1_DEPLOY_BUILT_AT", "2026-08-29T04:42:00Z")
    monkeypatch.setenv("JSB1_DEPLOY_HOSTNAME", "impl-jsb.mangagaki.net")

    settings = Settings(_env_file=None, data_dir=tmp_path / "data")
    info = load_build_info(settings)

    assert info.branch == "impl"
    assert info.commit == FULL_COMMIT
    assert info.short_commit == "8efb066"
    assert info.built_at == "2026-08-29T04:42:00Z"
    assert info.hostname == "impl-jsb.mangagaki.net"


def test_build_metadata_has_development_fallbacks(tmp_path) -> None:
    info = load_build_info(Settings(_env_file=None, data_dir=tmp_path / "data"))

    assert info.model_dump() == {
        "branch": "dev",
        "commit": "unknown",
        "short_commit": "unknown",
        "built_at": "unknown",
        "hostname": None,
    }


def test_version_endpoint_returns_immutable_build_metadata(settings) -> None:
    deployed_settings = settings.model_copy(
        update={
            "build_branch": "impl",
            "build_commit": FULL_COMMIT,
            "build_time": "2026-08-29T04:42:00Z",
            "build_hostname": "impl-jsb.mangagaki.net",
        }
    )

    with TestClient(create_app(deployed_settings, FakeSimulationRunner())) as client:
        response = client.get("/api/version")

    assert response.status_code == 200
    assert response.json() == {
        "branch": "impl",
        "commit": FULL_COMMIT,
        "short_commit": "8efb066",
        "built_at": "2026-08-29T04:42:00Z",
        "hostname": "impl-jsb.mangagaki.net",
    }
