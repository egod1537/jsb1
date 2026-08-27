import type { CreateRunInput, RunDetail, RunSummary, SignalResponse } from "../types/api";

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

export const api = {
  scenarios: () => request<string[]>("/api/scenarios"),
  autopilots: () => request<string[]>("/api/autopilots"),
  runs: (query = "") => request<RunSummary[]>(`/api/runs${query}`),
  run: (id: number) => request<RunDetail>(`/api/runs/${id}`),
  createRun: (input: CreateRunInput) =>
    request<{ id: number; status: string }>("/api/runs", {
      method: "POST",
      body: JSON.stringify(input),
    }),
  signals: (id: number, channels: string[], maxPoints = 2000) => {
    const params = new URLSearchParams({
      channels: channels.join(","),
      max_points: String(maxPoints),
    });
    return request<SignalResponse>(`/api/runs/${id}/signals?${params}`);
  },
};
