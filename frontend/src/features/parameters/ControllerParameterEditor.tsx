import { Button, Callout, FormGroup, Intent, NumericInput, Tag } from "@blueprintjs/core";
import type { ControllerParameterDefinition } from "../../types/api";
import { runtimeParameterViewModel } from "./parameterViewModel";
import "./parameters.css";

export type ControllerParameterDraftValues = Record<string, string>;

interface Props {
  definitions: ControllerParameterDefinition[];
  draftValues: ControllerParameterDraftValues;
  variants?: string[];
  selectedCategoryId?: string;
  loading?: boolean;
  error?: string | null;
  fieldErrors?: Record<string, string>;
  onBlur?: (parameterId: string) => void;
  onChange: (values: ControllerParameterDraftValues) => void;
}

export interface ControllerParameterCategory {
  id: string;
  label: string;
}

function applicableDefinitions(
  definitions: ControllerParameterDefinition[],
  variant?: string | string[],
) {
  if (variant == null) return definitions;
  const variants = Array.isArray(variant) ? variant : [variant];
  return definitions.filter(
    (item) => (item.variants ?? []).length === 0 || item.variants.some((value) => variants.includes(value)),
  );
}

function categoryFromLabel(label: string): ControllerParameterCategory {
  const trimmed = label.trim();
  const id = trimmed.toLocaleLowerCase().replace(/[^a-z0-9]+/g, "-").replace(/^-|-$/g, "") || "other";
  return {
    id,
    label: trimmed.replace(/\b\w/g, (character) => character.toLocaleUpperCase()),
  };
}

export function controllerParameterCategory(
  definition: ControllerParameterDefinition,
): ControllerParameterCategory {
  const declared = definition.category?.trim() || definition.group?.trim();
  if (declared) return categoryFromLabel(declared);
  const declaredModule = definition.module?.split(".").at(-1);
  if (declaredModule) return categoryFromLabel(declaredModule);
  return { id: "other", label: "Other" };
}

export function controllerParameterCategories(
  definitions: ControllerParameterDefinition[],
  variant?: string | string[],
): ControllerParameterCategory[] {
  const categories = new Map<string, ControllerParameterCategory>();
  applicableDefinitions(definitions, variant).forEach((definition) => {
    const category = controllerParameterCategory(definition);
    if (!categories.has(category.id)) categories.set(category.id, category);
  });
  return [...categories.values()];
}

export function selectSupportedControllerParameterDefinitions(
  definitions: ControllerParameterDefinition[],
  requestedIds: string[],
  variants: string[],
  scenarioType?: string | null,
): ControllerParameterDefinition[] {
  const supported = new Map(applicableDefinitions(definitions, variants)
    .filter((definition) => !scenarioType || !definition.scenario_types?.length || definition.scenario_types.includes(scenarioType))
    .map((definition) => [definition.id, definition]));
  return requestedIds.flatMap((id) => {
    const definition = supported.get(id);
    return definition ? [definition] : [];
  });
}

export function defaultControllerParameterValues(
  definitions: ControllerParameterDefinition[],
  variant: string | string[],
): Record<string, number> {
  return Object.fromEntries(applicableDefinitions(definitions, variant)
    .map((item) => [item.id, item.default_value]));
}

export function defaultControllerParameterDraftValues(
  definitions: ControllerParameterDefinition[],
  variant?: string | string[],
): ControllerParameterDraftValues {
  return Object.fromEntries(applicableDefinitions(definitions, variant)
    .map((item) => [item.id, String(item.default_value * (item.display_scale ?? 1))]));
}

export function controllerParameterErrors(
  definitions: ControllerParameterDefinition[],
  values: Record<string, number>,
  variant: string | string[],
): string[] {
  return applicableDefinitions(definitions, variant).flatMap((item) => {
    const value = values[item.id];
    if (!Number.isFinite(value)) return [`${item.id} requires a finite value`];
    if (item.minimum != null && value < item.minimum) return [`${item.id} must be at least ${item.minimum}`];
    if (item.maximum != null && value > item.maximum) return [`${item.id} must be at most ${item.maximum}`];
    return [];
  });
}

export function parseControllerParameterDrafts(
  definitions: ControllerParameterDefinition[],
  draftValues: ControllerParameterDraftValues,
  variant?: string | string[],
): { values: Record<string, number>; errors: Record<string, string> } {
  const values: Record<string, number> = {};
  const errors: Record<string, string> = {};
  const numberPattern = /^[+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?$/;

  applicableDefinitions(definitions, variant).forEach((item) => {
    const valueText = draftValues[item.id]?.trim() ?? "";
    if (!numberPattern.test(valueText)) {
      errors[item.id] = `${item.id} requires a finite value`;
      return;
    }
    const value = Number(valueText);
    const scale = item.display_scale ?? 1;
    const minimum = item.minimum == null ? null : item.minimum * scale;
    const maximum = item.maximum == null ? null : item.maximum * scale;
    if (!Number.isFinite(value)) {
      errors[item.id] = `${item.id} requires a finite value`;
    } else if (minimum != null && value < minimum) {
      errors[item.id] = `${item.id} must be at least ${minimum}`;
    } else if (maximum != null && value > maximum) {
      errors[item.id] = `${item.id} must be at most ${maximum}`;
    } else {
      values[item.id] = value / scale;
    }
  });
  return { values, errors };
}

export function controllerParameterOverrides(
  definitions: ControllerParameterDefinition[],
  values: Record<string, number>,
  variant: string | string[],
): Record<string, number> {
  return Object.fromEntries(applicableDefinitions(definitions, variant)
    .filter((item) => values[item.id] !== item.default_value)
    .map((item) => [item.id, values[item.id]]));
}

export function ControllerParameterEditor({
  definitions,
  draftValues,
  variants,
  selectedCategoryId,
  loading = false,
  error,
  fieldErrors = {},
  onBlur,
  onChange,
}: Props) {
  const applicable = applicableDefinitions(definitions, variants);
  const visible = selectedCategoryId
    ? applicable.filter((item) => controllerParameterCategory(item).id === selectedCategoryId)
    : applicable;
  const defaults = defaultControllerParameterDraftValues(definitions, variants);
  const parsed = parseControllerParameterDrafts(definitions, draftValues, variants);
  const modified = Object.keys(parsed.errors).length > 0 || applicable.some(
    (item) => parsed.values[item.id] !== item.default_value,
  );
  const numberText = (value: number) => String(value);

  if (error) return <Callout compact intent="danger">{error}</Callout>;
  if (loading) return <Callout compact>Loading controller parameter metadata…</Callout>;
  if (applicable.length === 0) return <Callout compact intent="warning">
    No tunable controller parameters are declared for this headless execution contract.
  </Callout>;

  return <div className="controller-parameter-editor">
    <div className="controller-parameter-preset-row">
      <div><strong>Runtime Default</strong><small>Contract-defined defaults for this scenario</small></div>
      <Tag minimal intent={modified ? "warning" : "success"}>{modified ? "CUSTOM · MODIFIED" : "ACTIVE · DEFAULT"}</Tag>
      <Button
        disabled={!modified}
        minimal
        small
        onClick={() => onChange(defaults)}
        type="button"
      >Reset defaults</Button>
    </div>
    <div className="controller-parameter-grid">
      {visible.map((item) => {
        const field = runtimeParameterViewModel(item);
        const fieldError = fieldErrors[item.id];
        return <FormGroup
          helperText={<><span className="controller-parameter-description">{field.description}</span>{fieldError && <span className="controller-parameter-error">{fieldError}</span>}</>}
          intent={fieldError ? Intent.DANGER : Intent.NONE}
          key={item.id}
          label={<span className="controller-parameter-label"><strong>{field.label}</strong><code>{field.id}</code></span>}
        >
          <div className="controller-parameter-input-row">
            <NumericInput
              aria-label={field.id}
              clampValueOnBlur={false}
              fill
              intent={fieldError ? Intent.DANGER : Intent.NONE}
              majorStepSize={field.step * 10}
              max={field.maximum ?? undefined}
              min={field.minimum ?? undefined}
              minorStepSize={field.step / 10}
              onBlur={() => onBlur?.(item.id)}
              onValueChange={(_value, valueText) => onChange({ ...draftValues, [item.id]: valueText })}
              stepSize={field.step}
              value={draftValues[item.id] ?? String(field.defaultValue)}
            />
            {field.unit && <span className="controller-parameter-unit">{field.unit}</span>}
          </div>
          <div className="controller-parameter-range technical-value">
            Default {numberText(field.defaultValue)}
            {field.minimum != null && field.maximum != null ? ` · ${numberText(field.minimum)}–${numberText(field.maximum)}` : ""}
          </div>
        </FormGroup>;
      })}
    </div>
  </div>;
}
