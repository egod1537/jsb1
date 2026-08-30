ALTER TABLE runs ADD COLUMN current_stage TEXT;
ALTER TABLE runs ADD COLUMN pipeline_stages TEXT NOT NULL DEFAULT '[]';

ALTER TABLE builds ADD COLUMN current_stage TEXT;
ALTER TABLE builds ADD COLUMN pipeline_stages TEXT NOT NULL DEFAULT '[]';
