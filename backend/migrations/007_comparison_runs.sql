CREATE TABLE comparisons (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    scenario_id TEXT NOT NULL,
    scenario_source TEXT NOT NULL,
    scenario_name TEXT NOT NULL,
    scenario_type TEXT,
    scenario_sha256 TEXT NOT NULL,
    scenario_path TEXT NOT NULL,
    repository_id INTEGER NOT NULL REFERENCES repositories(id),
    branch TEXT NOT NULL,
    commit_sha TEXT NOT NULL,
    build_id INTEGER NOT NULL REFERENCES builds(id),
    created_at TEXT NOT NULL
);

ALTER TABLE runs ADD COLUMN execution_variant TEXT;
ALTER TABLE runs ADD COLUMN comparison_id INTEGER REFERENCES comparisons(id);

UPDATE runs
SET execution_variant = autopilot
WHERE execution_variant IS NULL;

CREATE UNIQUE INDEX idx_comparison_run_variant
ON runs(comparison_id, execution_variant)
WHERE comparison_id IS NOT NULL;

CREATE INDEX idx_runs_comparison_id ON runs(comparison_id);
