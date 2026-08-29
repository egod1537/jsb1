import type { Branch, Build, BuildVersion, CreateRunInput, Deployment, Repository, RunDetail, RunSummary, SignalResponse } from "../types/api";

export class ApiError extends Error {
  constructor(
    message: string,
    public readonly status: number,
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
    try {
      const body = (await response.json()) as { detail?: string };
      message = body.detail ?? message;
    } catch {
      // Keep the HTTP fallback when an intermediary returned non-JSON.
    }
    throw new ApiError(message, response.status);
  }
  return response.json() as Promise<T>;
}

async function requestVoid(path: string, init?: RequestInit): Promise<void> {
  const response = await fetch(path, init);
  if (!response.ok) throw new ApiError(`Request failed (${response.status})`, response.status);
}

export const api = {
  version: () => request<BuildVersion>("/api/version"),
  scenarios: () => request<string[]>("/api/scenarios"),
  autopilots: () => request<string[]>("/api/autopilots"),
  runs: (query = "") => request<RunSummary[]>(`/api/runs${query}`),
  run: (id: number) => request<RunDetail>(`/api/runs/${id}`),
  createRun: (input: CreateRunInput) =>
    request<{ id: number; status: string }>("/api/runs", {
      method: "POST",
      body: JSON.stringify(input),
    }),
  repositories: () => request<Repository[]>("/api/repositories"),
  repository: (id: number) => request<Repository>(`/api/repositories/${id}`),
  createRepository: (input: { name: string; remote_url: string; local_path: string; default_branch?: string }) =>
    request<Repository>("/api/repositories", { method: "POST", body: JSON.stringify(input) }),
  deleteRepository: (id: number) => requestVoid(`/api/repositories/${id}`, { method: "DELETE" }),
  fetchRepository: (id: number) => request<Repository>(`/api/repositories/${id}/fetch`, { method: "POST" }),
  branches: (id: number) => request<Branch[]>(`/api/repositories/${id}/branches`),
  builds: (repositoryId?: number) => request<Build[]>(`/api/builds${repositoryId ? `?repository_id=${repositoryId}` : ""}`),
  build: (id: number) => request<Build>(`/api/builds/${id}`),
  createBuild: (input: { repository_id: number; revision: string; rebuild?: boolean }) =>
    request<Build>("/api/builds", { method: "POST", body: JSON.stringify(input) }),
  rebuild: (id: number) => request<Build>(`/api/builds/${id}/rebuild`, { method: "POST" }),
  deployments: () => request<Deployment[]>("/api/deployments"),
  deployment: (id: number) => request<Deployment>(`/api/deployments/${id}`),
  createDeployment: (input: { repository_id: number; branch: string }) =>
    request<Deployment>("/api/deployments", { method: "POST", body: JSON.stringify(input) }),
  redeploy: (id: number) => request<Deployment>(`/api/deployments/${id}/redeploy`, { method: "POST" }),
  restartDeployment: (id: number) => request<Deployment>(`/api/deployments/${id}/restart`, { method: "POST" }),
  stopDeployment: (id: number, force = false) =>
    requestVoid(`/api/deployments/${id}${force ? "?force=true" : ""}`, { method: "DELETE" }),
  signals: (id: number, channels: string[], maxPoints = 2000) => {
    const params = new URLSearchParams({
      channels: channels.join(","),
      max_points: String(maxPoints),
    });
    return request<SignalResponse>(`/api/runs/${id}/signals?${params}`);
  },
};
