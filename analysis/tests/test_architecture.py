from __future__ import annotations

import ast
from pathlib import Path


def test_analysis_package_has_no_backend_or_transport_dependencies() -> None:
    source_root = Path(__file__).parents[1] / "src/jsb1_analysis"
    forbidden = {"app", "fastapi", "sqlite3"}
    violations: list[str] = []
    for source in source_root.rglob("*.py"):
        tree = ast.parse(source.read_text(encoding="utf-8"), filename=str(source))
        for node in ast.walk(tree):
            imports: list[str] = []
            if isinstance(node, ast.Import):
                imports = [alias.name for alias in node.names]
            elif isinstance(node, ast.ImportFrom) and node.module:
                imports = [node.module]
            for imported in imports:
                if imported.split(".", 1)[0] in forbidden:
                    violations.append(f"{source.relative_to(source_root)}: {imported}")
    assert violations == []
