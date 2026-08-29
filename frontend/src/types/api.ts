export type RunStatus = "queued" | "running" | "completed" | "failed";

export interface RunSummary {
  id: number;
  status: RunStatus;
  repository_id: number | null;
  repository_name: string | null;
  build_id: number | null;
  build_branch: string | null;
  commit_sha: string | null;
  scenario_name: string;
  autopilot: string;
  created_at: string;
  wall_time_sec: number | null;
}

export interface Run extends RunSummary {
  scenario_path: string;
  started_at: string | null;
  finished_at: string | null;
  exit_code: number | null;
  simulation_time_sec: number | null;
  output_directory: string | null;
  error_message: string | null;
}

export interface Metric {
  name: string;
  value: number | null;
  unit: string;
}

export interface Artifact {
  id: number;
  run_id: number;
  kind: string;
  filename: string;
  download_url: string;
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

export interface CreateRunInput {
  scenario: string;
  autopilot: string;
  commit_sha?: string;
  build_id?: number;
}

export interface Repository {
  id: number;
  name: string;
  remote_url: string;
  local_path: string;
  default_branch: string;
  created_at: string;
  updated_at: string;
  last_fetched_at: string | null;
  current_branch: string | null;
  head_commit: string;
  dirty: boolean;
  status: string;
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

export type DeploymentStatus = "queued" | "starting" | "running" | "failed" | "stopped";

export interface Deployment {
  id: number;
  repository_id: number;
  branch: string;
  commit_sha: string;
  slug: string;
  hostname: string;
  status: DeploymentStatus;
  frontend_port: number | null;
  backend_port: number | null;
  compose_project: string;
  worktree_path: string;
  created_at: string;
  started_at: string | null;
  stopped_at: string | null;
  updated_at: string;
  error_message: string | null;
}

export interface BuildVersion {
  branch: string;
  commit: string;
  short_commit: string;
  built_at: string;
  hostname: string | null;
}
