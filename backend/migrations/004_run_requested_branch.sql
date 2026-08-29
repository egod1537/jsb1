ALTER TABLE runs ADD COLUMN branch TEXT;

CREATE INDEX idx_runs_repository_branch ON runs(repository_id, branch);
