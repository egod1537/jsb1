import type { SignalMetadata } from "../../types/api";

export interface SignalDefinition {
  id: string;
  displayName: string;
  symbol?: string;
  symbolLatex?: string;
  unit: string;
  category: string;
  subcategory?: string;
  group?: string;
  description?: string;
}

function humanizeSignalId(id: string): string {
  return id
    .split("_")
    .filter(Boolean)
    .map((part) => part.charAt(0).toUpperCase() + part.slice(1))
    .join(" ");
}

export function signalDefinitionForId(id: string, unit = "raw"): SignalDefinition {
  return {
    id,
    displayName: humanizeSignalId(id),
    unit,
    category: "Other",
    subcategory: "Uncatalogued",
  };
}

function groupParts(group: string | null | undefined): [string | undefined, string | undefined] {
  const parts = group?.split(/[./:]/).map((part) => part.trim()).filter(Boolean) ?? [];
  return [parts[0], parts.slice(1).join(" ") || undefined];
}

export function normalizeSignalMetadata(signal: SignalMetadata): SignalDefinition {
  const fallback = signalDefinitionForId(signal.name, signal.unit);
  const [groupCategory, groupSubcategory] = groupParts(signal.group);
  return {
    id: signal.name,
    displayName: signal.display_name ?? fallback.displayName,
    symbol: signal.symbol ?? fallback.symbol,
    symbolLatex: signal.symbol_latex ?? fallback.symbolLatex,
    unit: signal.unit || fallback.unit,
    category: signal.category ?? groupCategory ?? fallback.category,
    subcategory: signal.subcategory ?? groupSubcategory ?? fallback.subcategory,
    group: signal.group ?? undefined,
    description: signal.description ?? undefined,
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
    .sort(([left], [right]) => left.localeCompare(right))
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
  return [signal.displayName, signal.id, signal.symbol, signal.symbolLatex, signal.unit, signal.group, signal.description]
    .filter((value): value is string => Boolean(value))
    .some((value) => value.toLocaleLowerCase().replaceAll("\\", "").includes(normalized.replaceAll("\\", "")));
}
