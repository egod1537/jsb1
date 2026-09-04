import type { SignalMetadata, SignalResponse } from "../../types/api";
import type { TimelineState } from "../../types/view";

export type { TimelineState } from "../../types/view";

export const PLOT_LAYOUTS = {
  "1x1": { columns: 1, rows: 1, capacity: 1, rowHeight: 560 },
  "1x2": { columns: 2, rows: 1, capacity: 2, rowHeight: 420 },
  "2x2": { columns: 2, rows: 2, capacity: 4, rowHeight: 360 },
  "2x3": { columns: 2, rows: 3, capacity: 6, rowHeight: 320 },
  "3x3": { columns: 3, rows: 3, capacity: 9, rowHeight: 300 },
} as const;

export type PlotLayoutId = keyof typeof PLOT_LAYOUTS;

export interface SignalReference {
  name: string;
  optional?: boolean;
}

export interface PlotDefinition {
  id: string;
  title: string;
  signals: SignalReference[];
}

export interface PlotPreset {
  id: string;
  name: string;
  description?: string;
  applicableScenarioTypes?: string[];
  recommendedLayout?: PlotLayoutId;
  category: "control" | "general" | "custom";
  plots: PlotDefinition[];
}

export interface PlotYAxis {
  mode: "auto" | "manual";
  min?: number;
  max?: number;
}

export interface PlotConfig extends PlotDefinition {
  yAxis: PlotYAxis;
  showLegend: boolean;
}

export interface PlotInstance extends PlotConfig {
  slot: number;
  sourcePlotId?: string;
  presentationVariant?: string;
}

export interface PlotLayoutItem {
  id: string;
  x: number;
  y: number;
  w: 1;
  h: 1;
}

export interface PlotSettingsValue {
  title: string;
  signals: SignalReference[];
  yAxis: PlotYAxis;
  showLegend: boolean;
}

export interface WorkspaceTelemetry extends SignalResponse {
  seriesTimes?: Record<string, number[]>;
}

export interface ResolvedPlotSeries {
  key: string;
  name: string;
  time: number[];
  values: number[];
  unit?: string;
}

export interface PlotDataSource {
  key: string;
  loadSignals(signals: string[]): Promise<WorkspaceTelemetry>;
  listSignals?(): Promise<SignalMetadata[]>;
  variants?: string[];
  resolveSignal?(signal: string, telemetry: WorkspaceTelemetry, variant?: string): ResolvedPlotSeries[];
  hasSignal?(signal: string, telemetry: WorkspaceTelemetry, variant?: string): boolean;
}

export function createPresetPlots(preset: PlotPreset): PlotConfig[] {
  return preset.plots.map((plot) => ({
    ...plot,
    signals: plot.signals.map((signal) => ({ ...signal })),
    yAxis: { mode: "auto" },
    showLegend: true,
  }));
}

export function createPresetLayoutItems(preset: PlotPreset, layout: PlotLayoutId): PlotLayoutItem[] {
  return preset.plots.map((plot, slot) => layoutItemForSlot(plot.id, slot, layout));
}

export function layoutItemForSlot(id: string, slot: number, layout: PlotLayoutId): PlotLayoutItem {
  const columns = PLOT_LAYOUTS[layout].columns;
  return { id, x: slot % columns, y: Math.floor(slot / columns), w: 1, h: 1 };
}

export function slotForLayoutItem(item: PlotLayoutItem, layout: PlotLayoutId): number {
  return item.y * PLOT_LAYOUTS[layout].columns + item.x;
}

export function reflowLayoutItems(
  items: PlotLayoutItem[],
  previousLayout: PlotLayoutId,
  nextLayout: PlotLayoutId,
): PlotLayoutItem[] {
  return items.map((item) => layoutItemForSlot(item.id, slotForLayoutItem(item, previousLayout), nextLayout));
}

export function arrangePlots(
  plots: PlotConfig[],
  items: PlotLayoutItem[],
  layout: PlotLayoutId,
): PlotInstance[] {
  const itemById = new Map(items.map((item) => [item.id, item]));
  return plots.flatMap((plot) => {
    const item = itemById.get(plot.id);
    return item ? [{ ...plot, slot: slotForLayoutItem(item, layout) }] : [];
  });
}

export function signalUnion(plots: PlotDefinition[]): string[] {
  return [...new Set(plots.flatMap((plot) => plot.signals.map((signal) => signal.name)))].sort();
}

export function signalNames(plot: PlotDefinition): string[] {
  return plot.signals.map((signal) => signal.name);
}
