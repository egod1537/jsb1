CREATE TABLE scenario_catalog (
    source TEXT NOT NULL,
    scenario_id TEXT NOT NULL,
    relative_path TEXT NOT NULL,
    cache_path TEXT,
    name TEXT,
    autopilot TEXT,
    sha256 TEXT,
    valid INTEGER NOT NULL DEFAULT 0,
    active INTEGER NOT NULL DEFAULT 1,
    validated_commit TEXT,
    last_validated_at TEXT,
    last_sync_at TEXT,
    last_error TEXT,
    last_error_commit TEXT,
    last_error_at TEXT,
    PRIMARY KEY (source, scenario_id)
);

CREATE INDEX idx_scenario_catalog_active_valid
    ON scenario_catalog(source, active, valid);

CREATE TABLE scenario_sync_state (
    source TEXT PRIMARY KEY,
    reachable INTEGER,
    last_sync_at TEXT,
    last_success_at TEXT,
    last_error TEXT
);

ALTER TABLE runs ADD COLUMN scenario_id TEXT;
ALTER TABLE runs ADD COLUMN scenario_source TEXT;
ALTER TABLE runs ADD COLUMN scenario_sha256 TEXT;
