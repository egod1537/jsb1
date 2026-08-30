import { useCallback, useEffect, useState } from "react";
import { api } from "../../api/client";
import type { RunDetail, RunSummary, SignalResponse } from "../../types/api";

interface AsyncState<T> {
  data: T | null;
  loading: boolean;
  error: string | null;
}

export function useRuns(poll = true): AsyncState<RunSummary[]> & { reload: () => Promise<void> } {
  const [state, setState] = useState<AsyncState<RunSummary[]>>({
    data: null,
    loading: true,
    error: null,
  });
  const load = useCallback(() => api.runs()
      .then((data) => setState({ data, loading: false, error: null }))
      .catch((error: Error) => setState({ data: null, loading: false, error: error.message })), []);
  useEffect(() => {
    load();
    if (!poll) return;
    const timer = window.setInterval(load, 3000);
    return () => window.clearInterval(timer);
  }, [load, poll]);
  return { ...state, reload: load };
}

export function useRun(id: number): AsyncState<RunDetail> {
  const [state, setState] = useState<AsyncState<RunDetail>>({
    data: null,
    loading: true,
    error: null,
  });
  useEffect(() => {
    let active = true;
    let timer: number | undefined;
    const load = () => {
      api.run(id)
        .then((data) => {
          if (!active) return;
          setState({ data, loading: false, error: null });
          if (data.run.status === "queued" || data.run.status === "running") {
            timer = window.setTimeout(load, 1500);
          }
        })
        .catch((error: Error) => {
          if (active) setState({ data: null, loading: false, error: error.message });
        });
    };
    load();
    return () => {
      active = false;
      if (timer) window.clearTimeout(timer);
    };
  }, [id]);
  return state;
}

export function useSignals(
  id: number,
  enabled: boolean,
  channels: string[],
): AsyncState<SignalResponse> {
  const [state, setState] = useState<AsyncState<SignalResponse>>({
    data: null,
    loading: enabled,
    error: null,
  });
  const channelKey = channels.join(",");
  useEffect(() => {
    if (!enabled) {
      setState({ data: null, loading: false, error: null });
      return;
    }
    let active = true;
    setState({ data: null, loading: true, error: null });
    api.signals(id, channels)
      .then((data) => active && setState({ data, loading: false, error: null }))
      .catch((error: Error) => {
        if (active) setState({ data: null, loading: false, error: error.message });
      });
    return () => {
      active = false;
    };
  }, [id, enabled, channelKey]); // eslint-disable-line react-hooks/exhaustive-deps
  return state;
}
