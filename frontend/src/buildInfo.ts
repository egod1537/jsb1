export interface BuildInfo {
  branch: string;
  commit: string;
  shortCommit: string;
  builtAt: string;
  hostname?: string;
}

export interface BuildEnvironment {
  VITE_JSB1_BRANCH?: string;
  VITE_JSB1_COMMIT?: string;
  VITE_JSB1_BUILD_TIME?: string;
  VITE_JSB1_HOSTNAME?: string;
}

export const JSB1_REPOSITORY_URL = "https://github.com/egod1537/jsb1";

function valueOr(value: string | undefined, fallback: string) {
  const normalized = value?.trim();
  return normalized || fallback;
}

export function parseBuildInfo(environment: BuildEnvironment): BuildInfo {
  const commit = valueOr(environment.VITE_JSB1_COMMIT, "unknown");
  const hostname = environment.VITE_JSB1_HOSTNAME?.trim() || undefined;
  return {
    branch: valueOr(environment.VITE_JSB1_BRANCH, "dev"),
    commit,
    shortCommit: commit === "unknown" ? "unknown" : commit.slice(0, 7),
    builtAt: valueOr(environment.VITE_JSB1_BUILD_TIME, "unknown"),
    ...(hostname ? { hostname } : {}),
  };
}

export function githubCommitUrl(info: BuildInfo) {
  if (info.commit === "unknown") return undefined;
  return `${JSB1_REPOSITORY_URL}/commit/${encodeURIComponent(info.commit)}`;
}

export const buildInfo = parseBuildInfo(
  (import.meta as ImportMeta & { readonly env: BuildEnvironment }).env,
);
