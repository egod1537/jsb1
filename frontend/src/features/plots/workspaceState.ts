import { useCallback, useEffect, useMemo, useRef, useState } from "react";
import {
  PLOT_LAYOUTS,
  createPresetLayoutItems,
  createPresetPlots,
  layoutItemForSlot,
  reflowLayoutItems,
  slotForLayoutItem,
  type PlotConfig,
  type PlotLayoutItem,
  type PlotLayoutId,
  type PlotPreset,
  type PlotSettingsValue,
} from "./plotTypes";
import { ANALYSIS_PRESET_REGISTRY, CUSTOM_PRESET, defaultPresetForScenario } from "./presets";

export interface PlotWorkspaceState {
  layout: PlotLayoutId;
  activePresetId: string;
  presetModified: boolean;
  plots: PlotConfig[];
  layoutItems: PlotLayoutItem[];
}

export interface PlotWorkspaceController extends PlotWorkspaceState {
  setLayout: (layout: PlotLayoutId) => void;
  applyPreset: (preset: PlotPreset) => void;
  addPlot: (slot: number, settings: PlotSettingsValue) => void;
  updatePlot: (plotId: string, settings: PlotSettingsValue) => void;
  removePlot: (plotId: string) => void;
  movePlot: (plotId: string, targetSlot: number) => void;
}

interface Options {
  scenarioType?: string | null;
  initialLayout?: PlotLayoutId;
  initialPresetId?: string;
  comparisonView?: "overlay" | "side-by-side";
  variantCount?: number;
}

export function usePlotWorkspaceController({
  scenarioType,
  initialLayout,
  initialPresetId,
  comparisonView = "overlay",
  variantCount = 0,
}: Options): PlotWorkspaceController {
  const [initialPreset] = useState(() => {
    const recommended = defaultPresetForScenario(scenarioType);
    return ANALYSIS_PRESET_REGISTRY.get(initialPresetId ?? recommended.id) ?? recommended;
  });
  const [layout, setLayout] = useState<PlotLayoutId>(initialLayout ?? initialPreset.recommendedLayout ?? "2x2");
  const [activePresetId, setActivePresetId] = useState(initialPreset.id);
  const [presetModified, setPresetModified] = useState(false);
  const [plots, setPlots] = useState<PlotConfig[]>(() => createPresetPlots(initialPreset));
  const [layoutItems, setLayoutItems] = useState<PlotLayoutItem[]>(() =>
    createPresetLayoutItems(initialPreset, initialLayout ?? initialPreset.recommendedLayout ?? "2x2"));
  const layoutRef = useRef(layout);
  const customPlotSequence = useRef(1);

  const changeLayout = useCallback((nextLayout: PlotLayoutId) => {
    const previousLayout = layoutRef.current;
    if (previousLayout === nextLayout) return;
    layoutRef.current = nextLayout;
    setLayoutItems((current) => reflowLayoutItems(current, previousLayout, nextLayout));
    setLayout(nextLayout);
  }, []);

  useEffect(() => {
    if (comparisonView !== "side-by-side" || variantCount <= 0) return;
    const presentedPlotCount = plots.length * variantCount;
    if (presentedPlotCount > PLOT_LAYOUTS[layout].capacity) {
      changeLayout(presentedPlotCount <= PLOT_LAYOUTS["2x3"].capacity ? "2x3" : "3x3");
    }
  }, [changeLayout, comparisonView, layout, plots.length, variantCount]);

  const markModified = useCallback(() => {
    if (activePresetId !== CUSTOM_PRESET.id) setPresetModified(true);
  }, [activePresetId]);
  const applyPreset = useCallback((preset: PlotPreset) => {
    setActivePresetId(preset.id);
    setPresetModified(false);
    setPlots(createPresetPlots(preset));
    setLayoutItems(createPresetLayoutItems(preset, layoutRef.current));
  }, []);
  const addPlot = useCallback((slot: number, settings: PlotSettingsValue) => {
    const sequence = customPlotSequence.current++;
    const id = `custom-${sequence}`;
    setPlots((current) => [...current, {
      id,
      title: settings.title,
      signals: settings.signals,
      yAxis: settings.yAxis,
      showLegend: settings.showLegend,
    }]);
    setLayoutItems((current) => [...current, layoutItemForSlot(id, slot, layoutRef.current)]);
    markModified();
  }, [markModified]);
  const updatePlot = useCallback((plotId: string, settings: PlotSettingsValue) => {
    setPlots((current) => current.map((plot) => plot.id === plotId ? {
      ...plot,
      title: settings.title,
      signals: settings.signals,
      yAxis: settings.yAxis,
      showLegend: settings.showLegend,
    } : plot));
    markModified();
  }, [markModified]);
  const removePlot = useCallback((plotId: string) => {
    setPlots((current) => current.filter((plot) => plot.id !== plotId));
    setLayoutItems((current) => current.filter((item) => item.id !== plotId));
    markModified();
  }, [markModified]);
  const movePlot = useCallback((plotId: string, targetSlot: number) => {
    const currentLayout = layoutRef.current;
    if (targetSlot < 0 || targetSlot >= PLOT_LAYOUTS[currentLayout].capacity) return;
    const source = layoutItems.find((item) => item.id === plotId);
    if (!source) return;
    const sourceSlot = slotForLayoutItem(source, currentLayout);
    if (sourceSlot === targetSlot) return;
    const target = layoutItems.find((item) => slotForLayoutItem(item, currentLayout) === targetSlot);
    setLayoutItems((current) => current.map((item) => {
      if (item.id === plotId) return layoutItemForSlot(item.id, targetSlot, currentLayout);
      if (target && item.id === target.id) return layoutItemForSlot(item.id, sourceSlot, currentLayout);
      return item;
    }));
    markModified();
  }, [layoutItems, markModified]);

  return useMemo(() => ({
    layout,
    activePresetId,
    presetModified,
    plots,
    layoutItems,
    setLayout: changeLayout,
    applyPreset,
    addPlot,
    updatePlot,
    removePlot,
    movePlot,
  }), [activePresetId, addPlot, applyPreset, changeLayout, layout, layoutItems, movePlot, plots, presetModified, removePlot, updatePlot]);
}
