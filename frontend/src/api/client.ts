import type { AvailableSignalsResponse, Branch, Build, BuildVersion, CourseHoldAnalysisVariants, CreateRunInput, CreateRunResponse, PitchHoldAnalysisVariants, RollHoldAnalysis, RollHoldAnalysisVariants, RunDetail, RuntimeControllerParameters, RuntimeRepository, RuntimeVariants, RunSummary, ScenarioCatalogEntry, ScenarioCreateResponse, ScenarioInspectionCatalogEntry, ScenarioInspectionDetail, ScenarioInspectionSource, ScenarioSyncResult, ScenarioSyncStatus, ScenarioValidationResult, SignalResponse, TecsAnalysisVariants } from "../types/api";

export class ApiError extends Error {
  constructor(
    message: string,
    public readonly status: number,
    public readonly detail?: unknown,
  ) {
    super(message);
  }
}

async function request<T>(path: string, init?: RequestInit): Promise<T> {
  const response = await fetch(path, {
    ...init,
    headers: { "Content-Type": "application/json", ...init?.headers },
  });
  if (!response.ok) {
    let message = `Request failed (${response.status})`;
    let detail: unknown;
    try {
      const body = (await response.json()) as { detail?: string | { message?: string } };
      detail = body.detail;
      message = typeof body.detail === "string"
        ? body.detail
        : body.detail?.message ?? message;
    } catch {
      // Keep the HTTP fallback when an intermediary returned non-JSON.
    }
    throw new ApiError(message, response.status, detail);
  }
  return response.json() as Promise<T>;
}

async function requestVoid(path: string, init?: RequestInit): Promise<void> {
  const response = await fetch(path, init);
  if (!response.ok) {
    let message = `Request failed (${response.status})`;
    try {
      const body = (await response.json()) as { detail?: string };
      message = body.detail ?? message;
    } catch {
      // Keep the HTTP fallback when an intermediary returned non-JSON.
    }
    throw new ApiError(message, response.status);
  }
}

export const api = {
  version: () => request<BuildVersion>("/api/version"),
  scenarios: () => request<ScenarioCatalogEntry[]>("/api/scenarios"),
  scenarioCatalog: () => request<ScenarioInspectionCatalogEntry[]>("/api/scenario-catalog"),
  scenarioDetail: (source: Exclude<ScenarioInspectionSource, "run_snapshot">, id: string) => {
    const params = new URLSearchParams({ source, id });
    return request<ScenarioInspectionDetail>(`/api/scenario-catalog/detail?${params}`);
  },
  validateScenario: (yaml: string) => request<ScenarioValidationResult>("/api/scenarios/validate", {
    method: "POST",
    body: JSON.stringify({ yaml }),
  }),
  createScenario: (path: string, yaml: string) => request<ScenarioCreateResponse>("/api/scenarios", {
    method: "POST",
    body: JSON.stringify({ path, yaml }),
  }),
  runScenario: (id: number) => request<ScenarioInspectionDetail>(`/api/runs/${id}/scenario`),
  scenarioSyncStatus: () => request<ScenarioSyncStatus>("/api/scenarios/sync/status"),
  syncScenarios: () => request<ScenarioSyncResult>("/api/scenarios/sync", { method: "POST" }),
  runtimeRepository: () => request<RuntimeRepository>("/api/runtime/repository"),
  fetchRuntimeRepository: () => request<RuntimeRepository>("/api/runtime/repository/fetch", { method: "POST" }),
  runtimeBranches: () => request<Branch[]>("/api/runtime/branches"),
  runtimeVariants: (branch?: string) => request<RuntimeVariants>(`/api/runtime/variants${branch ? `?${new URLSearchParams({ branch })}` : ""}`),
  runtimeParameters: (branch?: string, commit_sha?: string | null) => {
    const query = new URLSearchParams();
    if (branch) query.set("branch", branch);
    if (commit_sha) query.set("commit_sha", commit_sha);
    return request<RuntimeControllerParameters>(`/api/runtime/parameters${query.size ? `?${query}` : ""}`);
  },
  runs: (query = "") => request<RunSummary[]>(`/api/runs${query}`),
  run: (id: number) => request<RunDetail>(`/api/runs/${id}`),
  rollHoldAnalysis: (id: number) => request<RollHoldAnalysisVariants | RollHoldAnalysis>(`/api/runs/${id}/analysis/roll-hold`),
  courseHoldAnalysis: (id: number) => request<CourseHoldAnalysisVariants>(`/api/runs/${id}/analysis/course-hold`),
  pitchHoldAnalysis: (id: number) => request<PitchHoldAnalysisVariants>(`/api/runs/${id}/analysis/pitch-hold`),
  tecsAnalysis: (id: number) => request<TecsAnalysisVariants>(`/api/runs/${id}/analysis/tecs`),
  createRun: (input: CreateRunInput) =>
    request<CreateRunResponse>("/api/runs", {
      method: "POST",
      body: JSON.stringify(input),
    }),
  deleteRun: (id: number) => requestVoid(`/api/runs/${id}`, { method: "DELETE" }),
  builds: (repositoryId?: number) => request<Build[]>(`/api/builds${repositoryId ? `?repository_id=${repositoryId}` : ""}`),
  build: (id: number) => request<Build>(`/api/builds/${id}`),
  createBuild: (input: { repository_id: number; revision: string; rebuild?: boolean }) =>
    request<Build>("/api/builds", { method: "POST", body: JSON.stringify(input) }),
  rebuild: (id: number) => request<Build>(`/api/builds/${id}/rebuild`, { method: "POST" }),
  signals: (id: number, signals: string[], maxPoints = 2000, variant?: string) => {
    const params = new URLSearchParams({
      signals: signals.join(","),
      max_points: String(maxPoints),
    });
    if (variant) params.set("variant", variant);
    return request<SignalResponse>(`/api/runs/${id}/signals?${params}`);
  },
  availableSignals: (id: number) =>
    request<AvailableSignalsResponse>(`/api/runs/${id}/signals/available`),
};
