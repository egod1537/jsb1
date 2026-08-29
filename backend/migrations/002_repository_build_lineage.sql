PRAGMA foreign_keys = OFF;

CREATE TABLE repositories (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT NOT NULL UNIQUE,
    remote_url TEXT NOT NULL,
    local_path TEXT NOT NULL UNIQUE,
    default_branch TEXT NOT NULL,
    created_at TEXT NOT NULL,
    updated_at TEXT NOT NULL,
    last_fetched_at TEXT
);

CREATE TABLE builds (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    repository_id INTEGER NOT NULL REFERENCES repositories(id) ON DELETE RESTRICT,
    commit_sha TEXT NOT NULL,
    branch TEXT,
    status TEXT NOT NULL CHECK (status IN ('queued', 'running', 'completed', 'failed')),
    build_dir TEXT NOT NULL,
    executable_path TEXT,
    stdout_path TEXT NOT NULL,
    stderr_path TEXT NOT NULL,
    created_at TEXT NOT NULL,
    started_at TEXT,
    completed_at TEXT,
    error_message TEXT
);

CREATE INDEX idx_builds_repository_commit ON builds(repository_id, commit_sha);
CREATE INDEX idx_builds_status ON builds(status);
CREATE INDEX idx_builds_created_at ON builds(created_at DESC);

ALTER TABLE metrics RENAME TO metrics_legacy;
ALTER TABLE artifacts RENAME TO artifacts_legacy;
ALTER TABLE runs RENAME TO runs_legacy;

CREATE TABLE runs (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    status TEXT NOT NULL CHECK (status IN ('queued', 'running', 'completed', 'failed')),
    repository_id INTEGER REFERENCES repositories(id) ON DELETE RESTRICT,
    build_id INTEGER REFERENCES builds(id) ON DELETE RESTRICT,
    commit_sha TEXT,
    scenario_name TEXT NOT NULL,
    scenario_path TEXT NOT NULL,
    autopilot TEXT NOT NULL,
    created_at TEXT NOT NULL,
    started_at TEXT,
    finished_at TEXT,
    exit_code INTEGER,
    simulation_time_sec REAL,
    wall_time_sec REAL,
    output_directory TEXT,
    error_message TEXT
);

INSERT INTO runs (
    id, status, commit_sha, scenario_name, scenario_path, autopilot, created_at,
    started_at, finished_at, exit_code, simulation_time_sec, wall_time_sec,
    output_directory, error_message
)
SELECT id, status, commit_sha, scenario_name, scenario_path, autopilot, created_at,
       started_at, finished_at, exit_code, simulation_time_sec, wall_time_sec,
       output_directory, error_message
FROM runs_legacy;

CREATE TABLE metrics (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    run_id INTEGER NOT NULL REFERENCES runs(id) ON DELETE CASCADE,
    name TEXT NOT NULL,
    value REAL,
    unit TEXT NOT NULL,
    UNIQUE(run_id, name)
);

INSERT INTO metrics(id, run_id, name, value, unit)
SELECT id, run_id, name, value, unit FROM metrics_legacy;

CREATE TABLE artifacts (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    run_id INTEGER NOT NULL REFERENCES runs(id) ON DELETE CASCADE,
    kind TEXT NOT NULL,
    path TEXT NOT NULL,
    UNIQUE(run_id, kind)
);

INSERT INTO artifacts(id, run_id, kind, path)
SELECT id, run_id, kind, path FROM artifacts_legacy;

DROP TABLE metrics_legacy;
DROP TABLE artifacts_legacy;
DROP TABLE runs_legacy;

CREATE INDEX idx_runs_created_at ON runs(created_at DESC);
CREATE INDEX idx_runs_status ON runs(status);
CREATE INDEX idx_runs_scenario_name ON runs(scenario_name);
CREATE INDEX idx_runs_build_id ON runs(build_id);

CREATE TABLE instances (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    build_id INTEGER NOT NULL REFERENCES builds(id) ON DELETE RESTRICT,
    run_id INTEGER UNIQUE REFERENCES runs(id) ON DELETE CASCADE,
    pid INTEGER,
    status TEXT NOT NULL CHECK (status IN ('queued', 'running', 'stopped', 'failed')),
    started_at TEXT,
    stopped_at TEXT
);

CREATE INDEX idx_instances_build_id ON instances(build_id);
CREATE INDEX idx_instances_status ON instances(status);

PRAGMA foreign_keys = ON;
