import { Button, Callout, FormGroup, Intent, NumericInput, Tag } from "@blueprintjs/core";
import type { ControllerParameterDefinition } from "../../types/api";
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

const CATEGORY_ORDER = ["roll", "pitch", "airspeed", "course", "altitude", "other"];

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
  const id = definition.id.toLocaleUpperCase();
  if (/^FW_R(?:R)?_/.test(id)) return { id: "roll", label: "Roll" };
  if (/^FW_P(?:R)?_/.test(id)) return { id: "pitch", label: "Pitch" };

  const declared = definition.category?.trim() || definition.group?.trim();
  if (declared) return categoryFromLabel(declared);

  if (/(?:AIR_?SPEED|AIRSPEED|ASPD)/.test(id)) return { id: "airspeed", label: "Airspeed" };
  if (/(?:COURSE|CRS|HEADING|HDG)/.test(id)) return { id: "course", label: "Course" };
  if (/(?:ALTITUDE|ALT)/.test(id)) return { id: "altitude", label: "Altitude" };
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
  return [...categories.values()].sort((left, right) => {
    const leftOrder = CATEGORY_ORDER.indexOf(left.id);
    const rightOrder = CATEGORY_ORDER.indexOf(right.id);
    if (leftOrder === -1 && rightOrder === -1) return 0;
    if (leftOrder === -1) return CATEGORY_ORDER.length;
    if (rightOrder === -1) return -CATEGORY_ORDER.length;
    return leftOrder - rightOrder;
  });
}

export function selectSupportedControllerParameterDefinitions(
  definitions: ControllerParameterDefinition[],
  requestedIds: string[],
  variants: string[],
): ControllerParameterDefinition[] {
  const supported = new Map(applicableDefinitions(definitions, variants)
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
    .map((item) => [item.id, String(item.default_value)]));
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
    if (!Number.isFinite(value)) {
      errors[item.id] = `${item.id} requires a finite value`;
    } else if (item.minimum != null && value < item.minimum) {
      errors[item.id] = `${item.id} must be at least ${item.minimum}`;
    } else if (item.maximum != null && value > item.maximum) {
      errors[item.id] = `${item.id} must be at most ${item.maximum}`;
    } else {
      values[item.id] = value;
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
      <div><strong>PX4 Default</strong><small>Runtime-defined defaults for this Scenario</small></div>
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
        const configuredStep = item.step ?? item.increment ?? 0.01;
        const step = configuredStep > 0 ? configuredStep : 0.01;
        const fieldError = fieldErrors[item.id];
        return <FormGroup
          helperText={<><span className="controller-parameter-description">{item.description}</span>{fieldError && <span className="controller-parameter-error">{fieldError}</span>}</>}
          intent={fieldError ? Intent.DANGER : Intent.NONE}
          key={item.id}
          label={<span className="controller-parameter-label"><strong>{item.display_name}</strong><code>{item.id}</code></span>}
        >
          <div className="controller-parameter-input-row">
            <NumericInput
              aria-label={item.id}
              clampValueOnBlur={false}
              fill
              intent={fieldError ? Intent.DANGER : Intent.NONE}
              majorStepSize={step * 10}
              max={item.maximum ?? undefined}
              min={item.minimum ?? undefined}
              minorStepSize={step / 10}
              onBlur={() => onBlur?.(item.id)}
              onValueChange={(_value, valueText) => onChange({ ...draftValues, [item.id]: valueText })}
              stepSize={step}
              value={draftValues[item.id] ?? String(item.default_value)}
            />
            {item.unit && <span className="controller-parameter-unit">{item.unit}</span>}
          </div>
          <div className="controller-parameter-range technical-value">
            Default {numberText(item.default_value)}
            {item.minimum != null && item.maximum != null ? ` · ${numberText(item.minimum)}–${numberText(item.maximum)}` : ""}
          </div>
        </FormGroup>;
      })}
    </div>
  </div>;
}
