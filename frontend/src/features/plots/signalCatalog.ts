import type { SignalMetadata } from "../../types/api";

export interface SignalDefinition {
  id: string;
  displayName: string;
  symbol?: string;
  symbolLatex?: string;
  unit: string;
  category: string;
  subcategory?: string;
}

// Compatibility adapter for older JSB1 API responses. New responses carry
// this metadata from the backend's JSB0 contract adapter; definitions remain
// centralized here instead of being repeated by picker and plot components.
export const JSB0_SIGNAL_CATALOG: Readonly<Record<string, SignalDefinition>> = {
  commanded_roll: {
    id: "commanded_roll", displayName: "Commanded Roll", symbol: "φc", symbolLatex: "\\phi_c",
    unit: "deg", category: "Command", subcategory: "Roll",
  },
  commanded_roll_rate: {
    id: "commanded_roll_rate", displayName: "Commanded Roll Rate", symbol: "pc", symbolLatex: "p_c",
    unit: "deg/s", category: "Command", subcategory: "Roll",
  },
  roll: {
    id: "roll", displayName: "Roll", symbol: "φ", symbolLatex: "\\phi",
    unit: "deg", category: "Aircraft State", subcategory: "Attitude",
  },
  roll_rate: {
    id: "roll_rate", displayName: "Roll Rate", symbol: "p", symbolLatex: "p",
    unit: "deg/s", category: "Aircraft State", subcategory: "Angular Rates",
  },
  roll_error: {
    id: "roll_error", displayName: "Roll Error", symbol: "eφ", symbolLatex: "e_\\phi",
    unit: "deg", category: "Control", subcategory: "Tracking Error",
  },
  roll_rate_error: {
    id: "roll_rate_error", displayName: "Roll Rate Error", symbol: "ep", symbolLatex: "e_p",
    unit: "deg/s", category: "Control", subcategory: "Tracking Error",
  },
  aileron: {
    id: "aileron", displayName: "Aileron", symbol: "δa", symbolLatex: "\\delta_a",
    unit: "normalized", category: "Control", subcategory: "Surfaces",
  },
};

const CATEGORY_ORDER = ["Command", "Aircraft State", "Control", "Other"];

function humanizeSignalId(id: string): string {
  return id
    .split("_")
    .filter(Boolean)
    .map((part) => part.charAt(0).toUpperCase() + part.slice(1))
    .join(" ");
}

export function signalDefinitionForId(id: string, unit = "raw"): SignalDefinition {
  return JSB0_SIGNAL_CATALOG[id] ?? {
    id,
    displayName: humanizeSignalId(id),
    unit,
    category: "Other",
    subcategory: "Uncatalogued",
  };
}

export function normalizeSignalMetadata(signal: SignalMetadata): SignalDefinition {
  const fallback = signalDefinitionForId(signal.name, signal.unit);
  return {
    id: signal.name,
    displayName: signal.display_name ?? fallback.displayName,
    symbol: signal.symbol ?? fallback.symbol,
    symbolLatex: signal.symbol_latex ?? fallback.symbolLatex,
    unit: signal.unit || fallback.unit,
    category: signal.category ?? fallback.category,
    subcategory: signal.subcategory ?? fallback.subcategory,
  };
}

export interface SignalGroup {
  category: string;
  subcategories: Array<{ name: string; signals: SignalDefinition[] }>;
}

export function groupSignalDefinitions(signals: SignalDefinition[]): SignalGroup[] {
  const categories = new Map<string, Map<string, SignalDefinition[]>>();
  for (const signal of signals) {
    const subcategory = signal.subcategory ?? "General";
    const subcategories = categories.get(signal.category) ?? new Map<string, SignalDefinition[]>();
    const entries = subcategories.get(subcategory) ?? [];
    entries.push(signal);
    subcategories.set(subcategory, entries);
    categories.set(signal.category, subcategories);
  }
  return [...categories.entries()]
    .sort(([left], [right]) => {
      const leftIndex = CATEGORY_ORDER.indexOf(left);
      const rightIndex = CATEGORY_ORDER.indexOf(right);
      return (leftIndex < 0 ? CATEGORY_ORDER.length : leftIndex)
        - (rightIndex < 0 ? CATEGORY_ORDER.length : rightIndex)
        || left.localeCompare(right);
    })
    .map(([category, subcategories]) => ({
      category,
      subcategories: [...subcategories.entries()]
        .sort(([left], [right]) => left.localeCompare(right))
        .map(([name, entries]) => ({
          name,
          signals: entries.sort((left, right) => left.displayName.localeCompare(right.displayName)),
        })),
    }));
}

export function signalMatchesQuery(signal: SignalDefinition, query: string): boolean {
  const normalized = query.trim().toLocaleLowerCase();
  if (!normalized) return true;
  return [signal.displayName, signal.id, signal.symbol, signal.symbolLatex, signal.unit]
    .filter((value): value is string => Boolean(value))
    .some((value) => value.toLocaleLowerCase().replaceAll("\\", "").includes(normalized.replaceAll("\\", "")));
}
