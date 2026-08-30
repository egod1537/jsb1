import type { PlotDefinition, SignalReference } from "./plotTypes";

export interface PlotTemplate {
  id: string;
  name: string;
  category: "Roll Hold" | "Aircraft" | "Control";
  description: string;
  signals: SignalReference[];
}

export const PLOT_TEMPLATES: PlotTemplate[] = [
  {
    id: "roll-tracking", name: "Roll Tracking", category: "Roll Hold",
    description: "Commanded and measured roll attitude.",
    signals: [{ name: "commanded_roll" }, { name: "roll" }],
  },
  {
    id: "roll-rate-tracking", name: "Roll Rate Tracking", category: "Roll Hold",
    description: "Commanded and measured body roll rate.",
    signals: [{ name: "commanded_roll_rate" }, { name: "roll_rate" }],
  },
  {
    id: "roll-error", name: "Roll Error", category: "Roll Hold",
    description: "Roll-attitude tracking error published by JSB0.",
    signals: [{ name: "roll_error" }],
  },
  {
    id: "roll-rate-error", name: "Roll Rate Error", category: "Roll Hold",
    description: "Roll-rate tracking error published by JSB0.",
    signals: [{ name: "roll_rate_error" }],
  },
  {
    id: "roll-control", name: "Roll Control", category: "Roll Hold",
    description: "Roll command, response, and normalized aileron effort.",
    signals: [{ name: "commanded_roll" }, { name: "roll" }, { name: "aileron" }],
  },
  {
    id: "attitude", name: "Attitude", category: "Aircraft",
    description: "Contract-defined aircraft attitude signals.",
    signals: [{ name: "roll" }],
  },
  {
    id: "body-rates", name: "Body Rates", category: "Aircraft",
    description: "Contract-defined aircraft angular rates.",
    signals: [{ name: "roll_rate" }],
  },
  {
    id: "aileron", name: "Aileron", category: "Control",
    description: "Normalized lateral control-surface command.",
    signals: [{ name: "aileron", optional: true }],
  },
];

export const PLOT_TEMPLATE_REGISTRY = new Map(PLOT_TEMPLATES.map((template) => [template.id, template]));

export function plotFromTemplate(templateId: string, id = templateId, title?: string): PlotDefinition {
  const template = PLOT_TEMPLATE_REGISTRY.get(templateId);
  if (!template) throw new Error(`Unknown plot template: ${templateId}`);
  return {
    id,
    title: title ?? template.name,
    signals: template.signals.map((signal) => ({ ...signal })),
  };
}

export function validatePlotTemplateRegistry(templates: PlotTemplate[]): string[] {
  const ids = new Set<string>();
  const errors: string[] = [];
  for (const template of templates) {
    if (!template.id.trim()) errors.push("plot template id must not be empty");
    if (ids.has(template.id)) errors.push(`duplicate plot template id: ${template.id}`);
    ids.add(template.id);
    if (!template.name.trim()) errors.push(`plot template ${template.id} name must not be empty`);
    if (template.signals.length === 0) errors.push(`plot template ${template.id} must contain a signal`);
  }
  return errors;
}

const templateErrors = validatePlotTemplateRegistry(PLOT_TEMPLATES);
if (templateErrors.length > 0) throw new Error(`Invalid plot template registry: ${templateErrors.join("; ")}`);
