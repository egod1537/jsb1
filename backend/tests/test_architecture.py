from __future__ import annotations

import ast
from pathlib import Path

APP_ROOT = Path(__file__).resolve().parents[1] / "app"
PROJECT_ROOT = APP_ROOT.parents[1]


def imported_modules(path: Path) -> set[str]:
    tree = ast.parse(path.read_text(encoding="utf-8"), filename=str(path))
    modules: set[str] = set()
    for node in ast.walk(tree):
        if isinstance(node, ast.Import):
            modules.update(alias.name for alias in node.names)
        elif isinstance(node, ast.ImportFrom) and node.module:
            modules.add(node.module)
    return modules


def test_domain_does_not_depend_on_outer_layers() -> None:
    forbidden = (
        "app.api",
        "app.services",
        "app.repositories",
        "app.infrastructure",
        "app.analysis",
        "fastapi",
        "sqlite3",
        "subprocess",
        "paramiko",
        "mcap",
    )
    violations: list[str] = []
    for path in sorted((APP_ROOT / "domain").glob("*.py")):
        for module in imported_modules(path):
            if module.startswith(forbidden):
                violations.append(f"{path.name}: {module}")
    assert violations == []


def test_runtime_contract_paths_have_one_owner() -> None:
    markers = (
        "contract/index.json",
        "contract/VERSION",
        "contract/scenario/scenario.schema.json",
        "contract/execution/parameters.json",
        "contract/execution/parameter-set.schema.json",
        "contract/execution/variants.json",
        "contract/execution/capabilities.json",
        "contract/execution/artifacts.json",
        "contract/metadata/run.schema.json",
        "contract/catalog/signals.yaml",
    )
    owners: dict[str, list[str]] = {marker: [] for marker in markers}
    for path in sorted(APP_ROOT.rglob("*.py")):
        text = path.read_text(encoding="utf-8")
        for marker in markers:
            if marker in text:
                owners[marker].append(str(path.relative_to(APP_ROOT)))
    assert owners == {marker: ["services/runtime_contract.py"] for marker in markers}


def test_application_services_do_not_depend_on_transport_or_io_libraries() -> None:
    forbidden = (
        "app.api",
        "app.workers",
        "fastapi",
        "sqlite3",
        "subprocess",
        "paramiko",
    )
    violations: list[str] = []
    for path in sorted((APP_ROOT / "services").rglob("*.py")):
        for module in imported_modules(path):
            if module.startswith(forbidden):
                violations.append(f"{path.relative_to(APP_ROOT)}: {module}")
    assert violations == []


def test_persistence_and_infrastructure_do_not_depend_on_http_routes() -> None:
    violations: list[str] = []
    for layer in ("repositories", "infrastructure"):
        for path in sorted((APP_ROOT / layer).rglob("*.py")):
            for module in imported_modules(path):
                if module.startswith(("app.api", "fastapi")):
                    violations.append(f"{path.relative_to(APP_ROOT)}: {module}")
    assert violations == []


def test_api_routes_do_not_import_low_level_io_libraries() -> None:
    forbidden = (
        "app.repositories",
        "sqlite3",
        "subprocess",
        "paramiko",
        "shutil",
        "pathlib",
    )
    violations: list[str] = []
    route_paths = [
        path
        for path in sorted((APP_ROOT / "api").glob("*.py"))
        if path.name == "routes.py" or path.name.endswith("_routes.py")
    ]
    for path in route_paths:
        for module in imported_modules(path):
            if module.startswith(forbidden):
                violations.append(f"{path.relative_to(APP_ROOT)}: {module}")
    assert violations == []


def test_analysis_does_not_depend_on_application_services() -> None:
    violations: list[str] = []
    for path in sorted((APP_ROOT / "analysis").rglob("*.py")):
        for module in imported_modules(path):
            if module.startswith(("app.api", "app.services", "app.repositories")):
                violations.append(f"{path.relative_to(APP_ROOT)}: {module}")
    assert violations == []


def test_worker_has_an_independent_composition_root() -> None:
    modules = imported_modules(APP_ROOT / "worker.py")
    assert not any(
        module.startswith(
            ("app.api", "app.container", "app.repositories", "app.services")
        )
        for module in modules
    )


def test_deployment_and_repository_services_do_not_own_process_syntax() -> None:
    for name in ("deployment_manager.py", "repository_manager.py"):
        modules = imported_modules(APP_ROOT / "services" / name)
        assert "subprocess" not in modules
        assert "os" not in modules


def test_host_and_compatibility_deployment_ownership_is_separate() -> None:
    deploy = (PROJECT_ROOT / "deploy.sh").read_text(encoding="utf-8")
    manager = (APP_ROOT / "services" / "deployment_manager.py").read_text(
        encoding="utf-8"
    )
    assert "github_update_deployment_status" in deploy
    assert "github_update_commit_status" in deploy
    assert "github_update_deployment_status" not in manager
    assert "github_update_commit_status" not in manager
    assert "branch-deployments" not in manager


def test_run_creation_has_no_execution_adapter_dependency() -> None:
    modules = imported_modules(APP_ROOT / "services" / "run_creation.py")
    assert "app.services.execution" not in modules
    assert "app.services.runner" not in modules
    assert not any(
        module.startswith("app.infrastructure.execution") for module in modules
    )


def test_deployment_preserves_independent_execution_worker() -> None:
    compose = (PROJECT_ROOT / "compose.deploy.yaml").read_text(encoding="utf-8")
    deploy = (PROJECT_ROOT / "deploy.sh").read_text(encoding="utf-8")
    normalized = " ".join(deploy.split())

    assert "worker:" in compose
    assert "JSB1_EXECUTION_MODE: external" in compose
    assert '["python", "-m", "app.worker"]' in compose
    assert "up -d --no-deps --force-recreate --wait backend" in normalized
    assert "Preserving independent execution worker" in deploy
    assert "execution worker drain verification" in deploy
    assert "up -d --force-recreate --remove-orphans --wait" not in normalized
