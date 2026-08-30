import type { PlotPreset } from "../plotTypes";
import { CUSTOM_PRESET } from "./custom";
import { DYNAMICS_PRESET } from "./dynamics";
import { ROLL_HOLD_PRESET } from "./rollHold";

export { CUSTOM_PRESET, DYNAMICS_PRESET, ROLL_HOLD_PRESET };

export const ANALYSIS_PRESETS: PlotPreset[] = [ROLL_HOLD_PRESET, DYNAMICS_PRESET, CUSTOM_PRESET];
export const ANALYSIS_PRESET_REGISTRY = new Map(ANALYSIS_PRESETS.map((preset) => [preset.id, preset]));

export const DEFAULT_PRESET_BY_SCENARIO: Readonly<Record<string, string>> = {
  roll_hold: ROLL_HOLD_PRESET.id,
};

export interface PresetAvailability {
  available: number;
  total: number;
  requiredAvailable: number;
  requiredTotal: number;
  usable: boolean;
}

export function defaultPresetForScenario(scenarioType: string | null | undefined): PlotPreset {
  const presetId = scenarioType ? DEFAULT_PRESET_BY_SCENARIO[scenarioType] : undefined;
  return ANALYSIS_PRESET_REGISTRY.get(presetId ?? DYNAMICS_PRESET.id) ?? DYNAMICS_PRESET;
}

export function presetAvailability(preset: PlotPreset, availableSignals: Iterable<string>): PresetAvailability {
  const available = new Set(availableSignals);
  const references = [...new Map(
    preset.plots.flatMap((plot) => plot.signals).map((signal) => [signal.name, signal]),
  ).values()];
  const required = references.filter((signal) => !signal.optional);
  const availableCount = references.filter((signal) => available.has(signal.name)).length;
  const requiredAvailable = required.filter((signal) => available.has(signal.name)).length;
  return {
    available: availableCount,
    total: references.length,
    requiredAvailable,
    requiredTotal: required.length,
    usable: references.length === 0 || availableCount > 0,
  };
}

export function validatePresetRegistry(presets: PlotPreset[]): string[] {
  const errors: string[] = [];
  const presetIds = new Set<string>();
  for (const preset of presets) {
    if (!preset.id.trim()) errors.push("preset id must not be empty");
    if (presetIds.has(preset.id)) errors.push(`duplicate preset id: ${preset.id}`);
    presetIds.add(preset.id);
    if (!preset.name.trim()) errors.push(`preset ${preset.id} name must not be empty`);
    const plotIds = new Set<string>();
    for (const plot of preset.plots) {
      if (!plot.id.trim()) errors.push(`preset ${preset.id} has an empty plot id`);
      if (plotIds.has(plot.id)) errors.push(`preset ${preset.id} has duplicate plot id: ${plot.id}`);
      plotIds.add(plot.id);
      if (!plot.title.trim()) errors.push(`preset ${preset.id} plot ${plot.id} title must not be empty`);
      const signalNames = new Set<string>();
      for (const signal of plot.signals) {
        if (!signal.name.trim()) errors.push(`preset ${preset.id} plot ${plot.id} has an empty signal name`);
        if (signalNames.has(signal.name)) errors.push(`preset ${preset.id} plot ${plot.id} has duplicate signal: ${signal.name}`);
        signalNames.add(signal.name);
      }
    }
  }
  return errors;
}

const registryErrors = validatePresetRegistry(ANALYSIS_PRESETS);
if (registryErrors.length > 0) throw new Error(`Invalid analysis preset registry: ${registryErrors.join("; ")}`);
