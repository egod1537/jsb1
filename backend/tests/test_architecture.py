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
        "contract/scenario/scenario.schema.json",
        "contract/execution/variants.json",
        "contract/execution/capabilities.json",
        "contract/catalog/signals.yaml",
    )
    owners: dict[str, list[str]] = {marker: [] for marker in markers}
    for path in sorted(APP_ROOT.rglob("*.py")):
        text = path.read_text(encoding="utf-8")
        for marker in markers:
            if marker in text:
                owners[marker].append(str(path.relative_to(APP_ROOT)))
    assert owners == {
        marker: ["services/runtime_contract.py"] for marker in markers
    }


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
