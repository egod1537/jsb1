import { signalDefinitionForId } from "./signalCatalog";

/** Presentation-only labels for canonical JSB0 signal names. */
export function formatSignalName(name: string): string {
  return signalDefinitionForId(name).displayName;
}
