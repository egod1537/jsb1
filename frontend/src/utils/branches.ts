import type { Branch } from "../types/api";

export function uniqueBranches(records: Branch[], preferRemote = false): Branch[] {
  const unique = new Map<string, Branch>();
  for (const branch of records) {
    const previous = unique.get(branch.name);
    const preferred = preferRemote
      ? previous && !previous.remote && branch.remote
      : previous && previous.remote && !branch.remote;
    if (!previous || preferred) unique.set(branch.name, branch);
  }
  return [...unique.values()].sort((a, b) => a.name.localeCompare(b.name));
}
