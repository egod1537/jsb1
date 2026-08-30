import { Button, Callout, FormGroup, NumericInput, Tag } from "@blueprintjs/core";
import type { ControllerParameterDefinition } from "../../types/api";
import "./parameters.css";

interface Props {
  definitions: ControllerParameterDefinition[];
  values: Record<string, number>;
  variant?: string;
  variants?: string[];
  loading?: boolean;
  error?: string | null;
  onChange: (values: Record<string, number>) => void;
}

export function defaultControllerParameterValues(
  definitions: ControllerParameterDefinition[],
  variant: string | string[],
): Record<string, number> {
  const variants = Array.isArray(variant) ? variant : [variant];
  return Object.fromEntries(definitions
    .filter((item) => (item.variants ?? []).length === 0 || item.variants.some((value) => variants.includes(value)))
    .map((item) => [item.id, item.default_value]));
}

export function controllerParameterErrors(
  definitions: ControllerParameterDefinition[],
  values: Record<string, number>,
  variant: string | string[],
): string[] {
  const variants = Array.isArray(variant) ? variant : [variant];
  return definitions
    .filter((item) => (item.variants ?? []).length === 0 || item.variants.some((value) => variants.includes(value)))
    .flatMap((item) => {
      const value = values[item.id];
      if (!Number.isFinite(value)) return [`${item.id} requires a finite value`];
      if (item.minimum != null && value < item.minimum) return [`${item.id} must be at least ${item.minimum}`];
      if (item.maximum != null && value > item.maximum) return [`${item.id} must be at most ${item.maximum}`];
      return [];
    });
}

export function controllerParameterOverrides(
  definitions: ControllerParameterDefinition[],
  values: Record<string, number>,
  variant: string | string[],
): Record<string, number> {
  const variants = Array.isArray(variant) ? variant : [variant];
  return Object.fromEntries(definitions
    .filter((item) => (item.variants ?? []).length === 0 || item.variants.some((value) => variants.includes(value)))
    .filter((item) => values[item.id] !== item.default_value)
    .map((item) => [item.id, values[item.id]]));
}

export function ControllerParameterEditor({
  definitions,
  values,
  variant,
  variants: requestedVariants,
  loading = false,
  error,
  onChange,
}: Props) {
  const variants = requestedVariants ?? (variant ? [variant] : []);
  const applicable = definitions.filter(
    (item) => (item.variants ?? []).length === 0 || item.variants.some((value) => variants.includes(value)),
  );
  const defaults = defaultControllerParameterValues(definitions, variants);
  const modified = Object.keys(defaults).some((id) => values[id] !== defaults[id]);
  const numberText = (value: number) => Number.isInteger(value) ? String(value) : String(value);

  if (error) return <Callout compact intent="danger">{error}</Callout>;
  if (loading) return <Callout compact>Loading controller parameter metadata…</Callout>;
  if (applicable.length === 0) return <Callout compact intent="warning">
    No tunable controller parameters are declared for this headless execution contract.
  </Callout>;

  return <div className="controller-parameter-editor">
    <div className="controller-parameter-preset-row">
      <div><strong>PX4 Default</strong><small>Runtime-defined Roll Hold defaults</small></div>
      <Tag minimal intent={modified ? "warning" : "success"}>{modified ? "CUSTOM · MODIFIED" : "ACTIVE"}</Tag>
      <Button
        disabled={!modified}
        minimal
        small
        onClick={() => onChange(defaults)}
        type="button"
      >Reset defaults</Button>
    </div>
    <div className="controller-parameter-grid">
      {applicable.map((item) => <FormGroup
        key={item.id}
        label={<span className="controller-parameter-label"><strong>{item.display_name}</strong><code>{item.id}</code></span>}
        helperText={item.description}
      >
        <div className="controller-parameter-input-row">
          <NumericInput
            aria-label={item.id}
            fill
            majorStepSize={item.increment == null ? undefined : item.increment * 10}
            max={item.maximum ?? undefined}
            min={item.minimum ?? undefined}
            minorStepSize={item.increment == null ? undefined : item.increment / 10}
            onValueChange={(value) => onChange({ ...values, [item.id]: value })}
            stepSize={item.increment ?? 0.01}
            value={values[item.id] ?? item.default_value}
          />
          {item.unit && <span className="controller-parameter-unit">{item.unit}</span>}
        </div>
        <div className="controller-parameter-range technical-value">
          Default {numberText(item.default_value)}
          {item.minimum != null && item.maximum != null ? ` · ${numberText(item.minimum)}–${numberText(item.maximum)}` : ""}
        </div>
      </FormGroup>)}
    </div>
  </div>;
}
