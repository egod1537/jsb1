export type RunStatus = "queued" | "running" | "completed" | "failed";

export interface RunSummary {
  id: number;
  status: RunStatus;
  commit_sha: string;
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
  commit_sha: string;
}

