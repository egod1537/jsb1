import { useCallback, useEffect, useMemo, useRef, useState } from "react";
import { useSearchParams } from "react-router-dom";
import { api } from "../../api/client";
import type { ScenarioInspectionDetail } from "../../types/api";

export type ValidationFilter = "all" | "valid" | "invalid" | "unknown";

export function useScenarioLibrary() {
  const [catalog, setCatalog] = useState<Awaited<ReturnType<typeof api.scenarioCatalog>>>([]);
  const [detail, setDetail] = useState<ScenarioInspectionDetail | null>(null);
  const [syncStatus, setSyncStatus] = useState<Awaited<ReturnType<typeof api.scenarioSyncStatus>> | null>(null);
  const [isCatalogLoading, setIsCatalogLoading] = useState(true);
  const [isScenarioDetailLoading, setIsScenarioDetailLoading] = useState(false);
  const [syncing, setSyncing] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [detailError, setDetailError] = useState<string | null>(null);
  const [search, setSearch] = useState("");
  const [source, setSource] = useState("all");
  const [scenarioType, setScenarioType] = useState("all");
  const [validation, setValidation] = useState<ValidationFilter>("all");
  const [params, setParams] = useSearchParams();
  const cache = useRef(new Map<string, ScenarioInspectionDetail>());
  const selectedId = params.get("id");
  const selectedIdRef = useRef(selectedId);
  const setParamsRef = useRef(setParams);
  selectedIdRef.current = selectedId;
  setParamsRef.current = setParams;

  const loadCatalog = useCallback(async () => {
    const [items, status] = await Promise.all([
      api.scenarioCatalog(),
      api.scenarioSyncStatus(),
    ]);
    setCatalog(items);
    setSyncStatus(status);
    const currentId = selectedIdRef.current;
    if (currentId && !items.some((item) => item.id === currentId)) {
      setParamsRef.current({}, { replace: true });
    }
  }, []);

  useEffect(() => {
    let active = true;
    setIsCatalogLoading(true);
    void loadCatalog()
      .catch((reason: unknown) => {
        if (active) setError(reason instanceof Error ? reason.message : "Could not load scenario catalog");
      })
      .finally(() => { if (active) setIsCatalogLoading(false); });
    return () => { active = false; };
  }, [loadCatalog]);

  const selected = catalog.find((item) => item.id === selectedId) ?? null;
  useEffect(() => {
    if (!selected || selected.source === "run_snapshot") {
      setIsScenarioDetailLoading(false);
      return;
    }
    let active = true;
    const key = `${selected.id}:${selected.sha256 ?? "unknown"}`;
    const cached = cache.current.get(key);
    if (cached) {
      setDetail(cached);
      setDetailError(null);
      setIsScenarioDetailLoading(false);
      return;
    }
    setIsScenarioDetailLoading(true);
    setDetailError(null);
    void api.scenarioDetail(selected.source, selected.path)
      .then((value) => {
        if (!active) return;
        cache.current.set(key, value);
        setDetail(value);
      })
      .catch((reason: unknown) => {
        if (active) {
          setDetailError(reason instanceof Error ? reason.message : "Could not load scenario");
          if (selectedIdRef.current === selected.id) setParamsRef.current({}, { replace: true });
        }
      })
      .finally(() => { if (active) setIsScenarioDetailLoading(false); });
    return () => { active = false; };
  }, [selected]);

  const types = useMemo(
    () => [...new Set(catalog.map((item) => item.scenario_type).filter(Boolean))].sort() as string[],
    [catalog],
  );
  const filtered = useMemo(() => catalog.filter((item) => {
    const query = search.trim().toLowerCase();
    const matchesSearch = !query || [item.name, item.path, item.scenario_type ?? ""]
      .some((value) => value.toLowerCase().includes(query));
    const matchesSource = source === "all" || item.source === source;
    const matchesType = scenarioType === "all" || item.scenario_type === scenarioType;
    const status = item.validation.valid === true ? "valid" : item.validation.valid === false ? "invalid" : "unknown";
    return matchesSearch && matchesSource && matchesType
      && (validation === "all" || validation === status);
  }), [catalog, scenarioType, search, source, validation]);

  const syncRemote = useCallback(async () => {
    setSyncing(true);
    setError(null);
    try {
      const result = await api.syncScenarios();
      cache.current.clear();
      await loadCatalog();
      if (!result.reachable) {
        setError(result.error ?? "Remote scenario sync failed; cached scenarios remain available");
      }
    } catch (reason) {
      setError(reason instanceof Error ? reason.message : "Could not sync remote scenarios");
    } finally {
      setSyncing(false);
    }
  }, [loadCatalog]);

  const refreshCatalog = useCallback(async () => {
    cache.current.clear();
    setError(null);
    try {
      await loadCatalog();
    } catch (reason) {
      setError(reason instanceof Error ? reason.message : "Could not refresh scenario catalog");
      throw reason;
    }
  }, [loadCatalog]);

  return {
    catalog, detail, syncStatus, isCatalogLoading, isScenarioDetailLoading,
    syncing, error,
    detailError, search, source, scenarioType, validation, selectedId,
    selected, types, filtered, setSearch, setSource, setScenarioType,
    setValidation,
    select: (id: string) => setParams({ id }),
    closeDetail: () => setParams({}, { replace: true }),
    syncRemote,
    refreshCatalog,
  };
}
