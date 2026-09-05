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
  const scale = definition.display_scale ?? 1;
  return {
    id: definition.id,
    label: definition.display_name,
    description: definition.description ?? "",
    unit: definition.display_unit ?? definition.unit ?? null,
    minimum: definition.minimum == null ? null : definition.minimum * scale,
    maximum: definition.maximum == null ? null : definition.maximum * scale,
    step: configuredStep > 0 ? configuredStep * scale : 0.01,
    defaultValue: definition.default_value * scale,
  };
}
