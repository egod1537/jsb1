export type RunStatus = "queued" | "running" | "completed" | "failed";

export type PipelineStageStatus = "pending" | "running" | "success" | "failed" | "skipped";

export interface PipelineStage {
  id: string;
  label: string;
  status: PipelineStageStatus;
  started_at: string | null;
  finished_at: string | null;
  duration_sec: number | null;
  message: string | null;
  error: string | null;
}

export interface RunSummary {
  id: number;
  status: RunStatus;
  repository_id: number | null;
  repository_name: string | null;
  branch: string | null;
  build_id: number | null;
  build_branch: string | null;
  commit_sha: string | null;
  scenario_name: string;
  scenario_type?: string | null;
  scenario_id?: string | null;
  scenario_source?: string | null;
  scenario_sha256?: string | null;
  autopilot: string;
  execution_variant: string;
  execution_mode?: string;
  variants?: string[];
  comparison_id: number | null;
  created_at: string;
  wall_time_sec: number | null;
  current_stage?: string | null;
}

export interface Run extends RunSummary {
  scenario_path: string;
  parameter_snapshot_path?: string | null;
  parameter_snapshot_sha256?: string | null;
  contract_version?: string | null;
  started_at: string | null;
  finished_at: string | null;
  exit_code: number | null;
  simulation_time_sec: number | null;
  output_directory: string | null;
  error_message: string | null;
  stages: PipelineStage[];
  controller_parameters: Record<string, number>;
  controller_parameter_overrides: Record<string, number>;
  variant_results?: Record<string, { status?: string; [key: string]: unknown }>;
  variant_parameters?: Record<string, Record<string, number>>;
}

export interface Metric {
  name: string;
  value: number | null;
  unit: string;
}

export interface AnalysisRegion {
  start_sec: number;
  end_sec: number;
}

export interface RollHoldAssessment {
  code: string;
  severity: "success" | "warning" | "info";
  message: string;
  start_sec: number | null;
  end_sec: number | null;
}

export interface RollHoldMarker {
  time_sec: number;
  value: number | null;
  label: string;
}

export interface RollHoldCheck {
  id: string;
  label: string;
  category: "tracking" | "dynamics" | "control";
  status: "pass" | "warn" | "fail" | "unavailable";
  actual: number | null;
  target: number | null;
  unit: string;
  target_source: "scenario" | "default" | "unavailable";
  message: string;
  start_sec: number | null;
  end_sec: number | null;
}

export interface RollHoldTarget {
  value: number | null;
  unit: string;
  source: "scenario" | "default" | "unavailable";
}

export interface RollHoldAnalysis {
  analyzer: "roll_hold";
  metrics: Record<string, number | boolean | null>;
  metric_units: Record<string, string>;
  parameters: Record<string, number | string | null>;
  targets: Record<string, RollHoldTarget>;
  regions: Record<string, AnalysisRegion>;
  intervals: Record<string, AnalysisRegion[]>;
  markers: Record<string, RollHoldMarker>;
  checks: RollHoldCheck[];
  assessment: RollHoldAssessment[];
  missing_signals: string[];
}

export interface RollHoldAnalysisVariants {
  variants: Record<string, RollHoldAnalysis>;
}

export interface Artifact {
  id: number;
  run_id: number;
  kind: string;
  filename: string;
  download_url: string;
  sha256?: string | null;
  size_bytes?: number | null;
}

export interface RunDetail {
  run: Run;
  metrics: Metric[];
  artifacts: Artifact[];
  instance: Instance | null;
}

export interface SignalResponse {
  time: number[];
  series: Record<string, number[]>;
  units: Record<string, string>;
  source_points: number;
  returned_points: number;
}

export interface SignalMetadata {
  name: string;
  unit: string;
  display_name?: string | null;
  symbol?: string | null;
  symbol_latex?: string | null;
  category?: string | null;
  subcategory?: string | null;
  contract_id?: string | null;
  topic?: string | null;
  field?: string | null;
  source_unit?: string | null;
  frame?: string | null;
  axis?: string | null;
  sign?: string | null;
  group?: string | null;
  description?: string | null;
  range?: Array<number | null> | null;
}

export interface AvailableSignalsResponse {
  signals: SignalMetadata[];
  variants?: Record<string, string[]>;
}

export interface CreateRunInput {
  scenario: string;
  scenario_source?: "bundled" | "managed" | "sftp";
  branch: string;
  controller_parameters?: Record<string, number>;
}

export interface ControllerParameterDefinition {
  id: string;
  display_name: string;
  category?: string | null;
  group?: string | null;
  symbol?: string | null;
  unit?: string | null;
  default_value: number;
  minimum?: number | null;
  maximum?: number | null;
  increment?: number | null;
  step?: number | null;
  description?: string | null;
  module?: string | null;
  controller?: string | null;
  type?: string;
  aircraft?: string[];
  algorithm_default?: number | null;
  profiles?: Record<string, { value: number }>;
  read_only?: boolean;
  experimental?: boolean;
  variants: string[];
}

export interface RuntimeControllerParameters {
  branch: string;
  commit_sha: string;
  source: "jsb0_contract" | "jsb1_px4_roll_hold_adapter";
  transport: string;
  parameters: ControllerParameterDefinition[];
}

export interface ScenarioCatalogEntry {
  id: string;
  name: string;
  source: "bundled" | "managed" | "sftp";
  autopilot?: string | null;
  valid: boolean;
  scenario_type?: string | null;
  schema_version?: number | null;
  controller_parameters?: string[];
  scenario_sha256: string;
  validated_runtime_commit: string | null;
  last_validated_at: string | null;
}

export interface ScenarioSyncStatus {
  configured: boolean;
  reachable: boolean | null;
  last_sync_at: string | null;
  last_success_at: string | null;
  last_error: string | null;
}

export interface ScenarioSyncResult {
  source: "sftp";
  configured: boolean;
  reachable: boolean;
  fetched: number;
  valid: number;
  invalid: number;
  updated: number;
  unchanged: number;
  removed: number;
  runtime_commit: string | null;
  error: string | null;
}

export type ScenarioInspectionSource = "bundled" | "managed" | "sftp" | "run_snapshot";

export interface ScenarioValidationError {
  path: string;
  code: string;
  message: string;
}

export interface ScenarioValidationResult {
  valid: boolean;
  scenario: { name: string | null; autopilot: string | null } | null;
  runtime: { branch: string; commit: string } | null;
  schema_version: number | null;
  errors: ScenarioValidationError[];
}

export interface ScenarioCreateResponse {
  id: string;
  source: "managed";
  path: string;
  scenario_sha256: string;
  validation: ScenarioValidationResult;
}

export interface ScenarioInspectionValidation {
  valid: boolean | null;
  runtime_branch: string | null;
  runtime_commit: string | null;
  errors: ScenarioValidationError[];
}

export interface ScenarioInspectionCatalogEntry {
  id: string;
  source: ScenarioInspectionSource;
  path: string;
  name: string;
  scenario_type: string | null;
  schema_version: number | null;
  controller_parameters?: string[];
  sha256: string | null;
  validation: ScenarioInspectionValidation;
  updated_at: string | null;
}

export interface ScenarioProvenance {
  authority: string;
  expected_sha256: string | null;
  actual_sha256: string | null;
  integrity: "verified" | "mismatch" | "unknown";
}

export interface ScenarioInspectionDetail extends ScenarioInspectionCatalogEntry {
  definition: Record<string, unknown> | null;
  raw_yaml: string | null;
  provenance: ScenarioProvenance;
}

export interface CreateRunResponse {
  id: number;
  status: RunStatus;
  repository_id: number | null;
  branch: string | null;
  commit_sha: string | null;
  build_id: number | null;
  build_status: BuildStatus | null;
  build_reused: boolean;
  execution_variant: string;
}

export interface RuntimeVariants {
  branch: string;
  commit_sha: string;
  mode?: string;
  variants: string[];
}

export interface RuntimeRepository {
  id: number;
  key: "jsb0";
  display_name: string;
  remote_url: string;
  local_path: string;
  default_branch: string;
  last_fetched_at: string | null;
  current_branch: string | null;
  head_commit: string;
  dirty: boolean;
  status: string;
  error: string | null;
  configuration_source: string;
}

export interface Branch {
  name: string;
  commit_sha: string;
  current: boolean;
  remote: boolean;
}

export type BuildStatus = "queued" | "running" | "completed" | "failed";

export interface Build {
  id: number;
  repository_id: number;
  repository_name: string | null;
  commit_sha: string;
  branch: string | null;
  status: BuildStatus;
  build_dir: string;
  executable_path: string | null;
  stdout_path: string;
  stderr_path: string;
  created_at: string;
  started_at: string | null;
  completed_at: string | null;
  error_message: string | null;
  reused: boolean;
  current_stage: string | null;
  stages: PipelineStage[];
}

export interface Instance {
  id: number;
  build_id: number;
  run_id: number | null;
  pid: number | null;
  status: "queued" | "running" | "stopped" | "failed";
  started_at: string | null;
  stopped_at: string | null;
}

export interface BuildVersion {
  branch: string;
  commit: string;
  short_commit: string;
  built_at: string;
  hostname: string | null;
}
