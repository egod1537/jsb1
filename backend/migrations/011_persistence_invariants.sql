-- Durable queue, immutable provenance, and artifact metadata invariants.

ALTER TABLE runs ADD COLUMN contract_version TEXT;
ALTER TABLE runs ADD COLUMN parameter_snapshot_path TEXT;
ALTER TABLE runs ADD COLUMN parameter_snapshot_sha256 TEXT;

ALTER TABLE artifacts ADD COLUMN sha256 TEXT;
ALTER TABLE artifacts ADD COLUMN size_bytes INTEGER;

-- Only one queued/running build may exist for a BuildKey. Historical terminal
-- attempts remain available for existing Run lineage and explicit rebuilds.
UPDATE builds
SET status = 'failed',
    completed_at = COALESCE(completed_at, strftime('%Y-%m-%dT%H:%M:%fZ', 'now')),
    error_message = COALESCE(error_message, 'duplicate active BuildKey removed by migration')
WHERE id IN (
    SELECT id FROM (
        SELECT id,
               row_number() OVER (
                   PARTITION BY repository_id, commit_sha
                   ORDER BY CASE status WHEN 'running' THEN 0 ELSE 1 END, id
               ) AS duplicate_number
        FROM builds
        WHERE status IN ('queued', 'running')
    )
    WHERE duplicate_number > 1
);

CREATE UNIQUE INDEX uq_builds_active_build_key
ON builds(repository_id, commit_sha)
WHERE status IN ('queued', 'running');

CREATE TABLE build_keys (
    repository_id INTEGER NOT NULL REFERENCES repositories(id) ON DELETE RESTRICT,
    commit_sha TEXT NOT NULL,
    build_id INTEGER NOT NULL UNIQUE REFERENCES builds(id) ON DELETE RESTRICT,
    PRIMARY KEY (repository_id, commit_sha)
);

INSERT INTO build_keys(repository_id, commit_sha, build_id)
SELECT repository_id, commit_sha, id
FROM (
    SELECT id, repository_id, commit_sha,
           row_number() OVER (
               PARTITION BY repository_id, commit_sha
               ORDER BY
                   CASE status
                       WHEN 'running' THEN 0
                       WHEN 'queued' THEN 1
                       WHEN 'completed' THEN 2
                       ELSE 3
                   END,
                   id DESC
           ) AS canonical_number
    FROM builds
)
WHERE canonical_number = 1;

CREATE TRIGGER trg_build_keys_matching_identity_insert
BEFORE INSERT ON build_keys
WHEN NOT EXISTS (
    SELECT 1 FROM builds
    WHERE id = NEW.build_id
      AND repository_id = NEW.repository_id
      AND commit_sha = NEW.commit_sha
)
BEGIN
    SELECT RAISE(ABORT, 'BuildKey identity does not match build');
END;

CREATE TRIGGER trg_build_keys_matching_identity_update
BEFORE UPDATE ON build_keys
WHEN NOT EXISTS (
    SELECT 1 FROM builds
    WHERE id = NEW.build_id
      AND repository_id = NEW.repository_id
      AND commit_sha = NEW.commit_sha
)
BEGIN
    SELECT RAISE(ABORT, 'BuildKey identity does not match build');
END;

CREATE INDEX idx_runs_durable_queue ON runs(status, id)
WHERE output_directory IS NOT NULL;

CREATE INDEX idx_builds_durable_queue ON builds(status, id);

CREATE TRIGGER trg_runs_valid_status_transition
BEFORE UPDATE OF status ON runs
WHEN NEW.status != OLD.status AND NOT (
    (OLD.status = 'queued' AND NEW.status IN ('running', 'failed')) OR
    (OLD.status = 'running' AND NEW.status IN ('completed', 'failed'))
)
BEGIN
    SELECT RAISE(ABORT, 'invalid run status transition');
END;

CREATE TRIGGER trg_builds_valid_status_transition
BEFORE UPDATE OF status ON builds
WHEN NEW.status != OLD.status AND NOT (
    (OLD.status = 'queued' AND NEW.status IN ('running', 'failed')) OR
    (OLD.status = 'running' AND NEW.status IN ('completed', 'failed'))
)
BEGIN
    SELECT RAISE(ABORT, 'invalid build status transition');
END;

CREATE TRIGGER trg_instances_valid_status_transition
BEFORE UPDATE OF status ON instances
WHEN NEW.status != OLD.status AND NOT (
    (OLD.status = 'queued' AND NEW.status IN ('running', 'failed')) OR
    (OLD.status = 'running' AND NEW.status IN ('stopped', 'failed'))
)
BEGIN
    SELECT RAISE(ABORT, 'invalid instance status transition');
END;

CREATE TRIGGER trg_deployments_valid_status_transition
BEFORE UPDATE OF status ON deployments
WHEN NEW.status != OLD.status AND NOT (
    (OLD.status = 'queued' AND NEW.status IN ('starting', 'failed', 'stopped')) OR
    (OLD.status = 'starting' AND NEW.status IN ('running', 'failed', 'stopped')) OR
    (OLD.status = 'running' AND NEW.status IN ('starting', 'stopped')) OR
    (OLD.status = 'failed' AND NEW.status = 'stopped')
)
BEGIN
    SELECT RAISE(ABORT, 'invalid deployment status transition');
END;

CREATE TRIGGER trg_artifacts_relative_metadata_insert
BEFORE INSERT ON artifacts
WHEN NEW.path = '' OR NEW.path LIKE '/%' OR instr(NEW.path, '\') > 0 OR
     NEW.path = '..' OR NEW.path LIKE '../%' OR
     NEW.path LIKE '%/../%' OR NEW.path LIKE '%/..' OR
     (NEW.sha256 IS NOT NULL AND (
        length(NEW.sha256) != 64 OR NEW.sha256 GLOB '*[^0-9a-f]*'
     )) OR
     (NEW.size_bytes IS NOT NULL AND NEW.size_bytes < 0)
BEGIN
    SELECT RAISE(ABORT, 'invalid artifact metadata');
END;

CREATE TRIGGER trg_artifacts_relative_metadata_update
BEFORE UPDATE OF path, sha256, size_bytes ON artifacts
WHEN NEW.path = '' OR NEW.path LIKE '/%' OR instr(NEW.path, '\') > 0 OR
     NEW.path = '..' OR NEW.path LIKE '../%' OR
     NEW.path LIKE '%/../%' OR NEW.path LIKE '%/..' OR
     (NEW.sha256 IS NOT NULL AND (
        length(NEW.sha256) != 64 OR NEW.sha256 GLOB '*[^0-9a-f]*'
     )) OR
     (NEW.size_bytes IS NOT NULL AND NEW.size_bytes < 0)
BEGIN
    SELECT RAISE(ABORT, 'invalid artifact metadata');
END;

CREATE TRIGGER trg_runs_valid_prepared_provenance
BEFORE UPDATE OF output_directory ON runs
WHEN OLD.output_directory IS NULL AND NEW.output_directory IS NOT NULL AND (
    NEW.output_directory = '' OR NEW.output_directory LIKE '/%' OR
    instr(NEW.output_directory, char(92)) > 0 OR
    NEW.output_directory = '..' OR NEW.output_directory LIKE '../%' OR
    NEW.output_directory LIKE '%/../%' OR NEW.output_directory LIKE '%/..' OR
    substr(NEW.scenario_path, 1, length(NEW.output_directory) + 1) !=
        NEW.output_directory || '/' OR
    NEW.scenario_sha256 IS NULL OR length(NEW.scenario_sha256) != 64 OR
    NEW.scenario_sha256 GLOB '*[^0-9a-f]*' OR
    (NEW.parameter_snapshot_path IS NULL) !=
        (NEW.parameter_snapshot_sha256 IS NULL) OR
    (NEW.parameter_snapshot_path IS NOT NULL AND
        (substr(NEW.parameter_snapshot_path, 1, length(NEW.output_directory) + 1) !=
             NEW.output_directory || '/' OR
         instr(NEW.parameter_snapshot_path, char(92)) > 0 OR
         NEW.parameter_snapshot_sha256 GLOB '*[^0-9a-f]*'))
)
BEGIN
    SELECT RAISE(ABORT, 'invalid prepared run provenance');
END;

-- output_directory is the durable readiness marker. Once it is set, every
-- execution input and its contract identity is immutable.
CREATE TRIGGER trg_runs_immutable_prepared_provenance
BEFORE UPDATE ON runs
WHEN OLD.output_directory IS NOT NULL AND (
    NEW.repository_id IS NOT OLD.repository_id OR
    NEW.branch IS NOT OLD.branch OR
    NEW.build_id IS NOT OLD.build_id OR
    NEW.commit_sha IS NOT OLD.commit_sha OR
    NEW.scenario_name IS NOT OLD.scenario_name OR
    NEW.scenario_type IS NOT OLD.scenario_type OR
    NEW.scenario_path IS NOT OLD.scenario_path OR
    NEW.scenario_id IS NOT OLD.scenario_id OR
    NEW.scenario_source IS NOT OLD.scenario_source OR
    NEW.scenario_sha256 IS NOT OLD.scenario_sha256 OR
    NEW.parameter_snapshot_path IS NOT OLD.parameter_snapshot_path OR
    NEW.parameter_snapshot_sha256 IS NOT OLD.parameter_snapshot_sha256 OR
    NEW.contract_version IS NOT OLD.contract_version OR
    NEW.autopilot IS NOT OLD.autopilot OR
    NEW.execution_variant IS NOT OLD.execution_variant OR
    NEW.execution_mode IS NOT OLD.execution_mode OR
    NEW.variants IS NOT OLD.variants OR
    NEW.controller_parameters IS NOT OLD.controller_parameters OR
    NEW.controller_parameter_overrides IS NOT OLD.controller_parameter_overrides OR
    NEW.variant_parameters IS NOT OLD.variant_parameters OR
    NEW.comparison_id IS NOT OLD.comparison_id OR
    NEW.output_directory IS NOT OLD.output_directory
)
BEGIN
    SELECT RAISE(ABORT, 'prepared run provenance is immutable');
END;
