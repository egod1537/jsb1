import { api, ApiError } from "../../api/client";
import type { SignalResponse } from "../../types/api";

const MISSING_CHANNELS_PREFIX = "channels not found:";

/**
 * Loads one de-duplicated workspace union. Older or partial MCAP artifacts may
 * omit canonical channels; in that specific case retry the remaining union
 * once instead of turning every panel into an independent request.
 */
export async function loadRunSignalUnion(
  runId: number,
  signals: string[],
  maxPoints = 4000,
  variant?: string,
): Promise<SignalResponse> {
  try {
    return variant
      ? await api.signals(runId, signals, maxPoints, variant)
      : await api.signals(runId, signals, maxPoints);
  } catch (error) {
    if (!(error instanceof ApiError) || error.status !== 422 || !error.message.startsWith(MISSING_CHANNELS_PREFIX)) throw error;
    const missing = new Set(error.message
      .slice(MISSING_CHANNELS_PREFIX.length)
      .split(",")
      .map((signal) => signal.trim())
      .filter(Boolean));
    const available = signals.filter((signal) => !missing.has(signal));
    if (available.length === 0 || available.length === signals.length) throw error;
    return variant
      ? api.signals(runId, available, maxPoints, variant)
      : api.signals(runId, available, maxPoints);
  }
}
