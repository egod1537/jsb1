import type { SignalMetadata } from "../../types/api";
import { normalizeSignalMetadata, signalDefinitionForId } from "./signalCatalog";

/** Presentation label supplied by the Runtime catalog, with a neutral fallback. */
export function formatSignalName(name: string, metadata?: SignalMetadata): string {
  return metadata ? normalizeSignalMetadata(metadata).displayName : signalDefinitionForId(name).displayName;
}
