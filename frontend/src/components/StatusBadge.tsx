import type { RunStatus } from "../types/api";

export function StatusBadge({ status }: { status: RunStatus }) {
  return <span className={`status status--${status}`}>{status}</span>;
}

