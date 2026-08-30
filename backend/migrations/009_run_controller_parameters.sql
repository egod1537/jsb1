ALTER TABLE runs ADD COLUMN controller_parameters TEXT NOT NULL DEFAULT '{}';
ALTER TABLE runs ADD COLUMN controller_parameter_overrides TEXT NOT NULL DEFAULT '{}';
