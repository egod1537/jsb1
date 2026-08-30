ALTER TABLE runs ADD COLUMN execution_mode TEXT NOT NULL DEFAULT 'single';
ALTER TABLE runs ADD COLUMN variants TEXT NOT NULL DEFAULT '[]';
ALTER TABLE runs ADD COLUMN variant_results TEXT NOT NULL DEFAULT '{}';
ALTER TABLE runs ADD COLUMN variant_parameters TEXT NOT NULL DEFAULT '{}';

UPDATE runs
SET variants = json_array(execution_variant)
WHERE execution_variant IS NOT NULL AND variants = '[]';
