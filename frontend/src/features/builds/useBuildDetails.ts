import { useEffect, useState } from "react";
import { api } from "../../api/client";
import type { Build } from "../../types/api";

export interface BuildDetailsState {
  data: Build | null;
  error: string | null;
  loading: boolean;
}

export function useBuildDetails(buildId: number | null): BuildDetailsState {
  const [state, setState] = useState<BuildDetailsState>({ data: null, error: null, loading: buildId != null });

  useEffect(() => {
    let active = true;
    let timer: number | undefined;
    if (buildId == null) {
      setState({ data: null, error: null, loading: false });
      return () => { active = false; };
    }

    setState({ data: null, error: null, loading: true });
    const load = async () => {
      try {
        const build = await api.build(buildId);
        if (!active) return;
        setState({ data: build, error: null, loading: false });
        if (build.status === "queued" || build.status === "running") {
          timer = window.setTimeout(load, 3000);
        }
      } catch (reason) {
        if (!active) return;
        setState({
          data: null,
          error: reason instanceof Error ? reason.message : "Build detail unavailable",
          loading: false,
        });
      }
    };
    void load();
    return () => {
      active = false;
      if (timer != null) window.clearTimeout(timer);
    };
  }, [buildId]);

  return state.data != null && state.data.id !== buildId
    ? { data: null, error: null, loading: buildId != null }
    : state;
}
