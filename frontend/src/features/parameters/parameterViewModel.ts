import type { ControllerParameterDefinition } from "../../types/api";

export interface RuntimeParameterViewModel {
  id: string;
  label: string;
  description: string;
  unit: string | null;
  minimum: number | null;
  maximum: number | null;
  step: number;
  defaultValue: number;
}

/** Maps the backend contract DTO to the shape consumed by parameter controls. */
export function runtimeParameterViewModel(
  definition: ControllerParameterDefinition,
): RuntimeParameterViewModel {
  const configuredStep = definition.step ?? definition.increment ?? 0.01;
  return {
    id: definition.id,
    label: definition.display_name,
    description: definition.description ?? "",
    unit: definition.unit ?? null,
    minimum: definition.minimum ?? null,
    maximum: definition.maximum ?? null,
    step: configuredStep > 0 ? configuredStep : 0.01,
    defaultValue: definition.default_value,
  };
}
