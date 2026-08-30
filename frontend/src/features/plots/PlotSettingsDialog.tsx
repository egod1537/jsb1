import {
  Button,
  Callout,
  Checkbox,
  Dialog,
  DialogBody,
  DialogFooter,
  FormGroup,
  HTMLSelect,
  InputGroup,
  Intent,
  NumericInput,
  Radio,
  RadioGroup,
} from "@blueprintjs/core";
import { IconNames } from "@blueprintjs/icons";
import { FormEvent, useEffect, useMemo, useState } from "react";
import type { SignalMetadata } from "../../types/api";
import type { PlotConfig, PlotSettingsValue } from "./plotTypes";
import { formatSignalName } from "./signalLabels";
import { PLOT_TEMPLATES } from "./plotTemplates";
import { SignalSymbol } from "./SignalSymbol";
import {
  groupSignalDefinitions,
  normalizeSignalMetadata,
  signalMatchesQuery,
} from "./signalCatalog";

interface Props {
  mode: "create" | "edit";
  isOpen: boolean;
  initialPlot?: PlotConfig;
  availableSignals: SignalMetadata[];
  onCancel: () => void;
  onApply: (settings: PlotSettingsValue) => void;
}

export function PlotSettingsDialog({ mode, isOpen, initialPlot, availableSignals, onCancel, onApply }: Props) {
  const [title, setTitle] = useState("");
  const [selectedSignals, setSelectedSignals] = useState<string[]>([]);
  const [signalQuery, setSignalQuery] = useState("");
  const [yAxisMode, setYAxisMode] = useState<"auto" | "manual">("auto");
  const [yMin, setYMin] = useState("");
  const [yMax, setYMax] = useState("");
  const [showLegend, setShowLegend] = useState(true);

  useEffect(() => {
    if (!isOpen) return;
    setTitle(initialPlot?.title ?? "");
    setSelectedSignals(initialPlot?.signals.map((signal) => signal.name) ?? []);
    setSignalQuery("");
    setYAxisMode(initialPlot?.yAxis.mode ?? "auto");
    setYMin(initialPlot?.yAxis.min == null ? "" : String(initialPlot.yAxis.min));
    setYMax(initialPlot?.yAxis.max == null ? "" : String(initialPlot.yAxis.max));
    setShowLegend(initialPlot?.showLegend ?? true);
  }, [initialPlot, isOpen, mode]);

  const signalDefinitions = useMemo(
    () => availableSignals.map(normalizeSignalMetadata),
    [availableSignals],
  );
  const filteredSignalGroups = useMemo(() => {
    const matches = signalDefinitions.filter((signal) => signalMatchesQuery(signal, signalQuery));
    return groupSignalDefinitions(matches);
  }, [signalDefinitions, signalQuery]);
  const templateGroups = useMemo(() => {
    const categories = new Map<string, typeof PLOT_TEMPLATES>();
    for (const template of PLOT_TEMPLATES) {
      const entries = categories.get(template.category) ?? [];
      entries.push(template);
      categories.set(template.category, entries);
    }
    return [...categories.entries()];
  }, []);
  const availableSignalIds = useMemo(
    () => new Set(signalDefinitions.map((signal) => signal.id)),
    [signalDefinitions],
  );
  const templateAvailability = (templateId: string) => {
    const template = PLOT_TEMPLATES.find((candidate) => candidate.id === templateId);
    return template?.signals.filter((signal) => availableSignalIds.has(signal.name)).length ?? 0;
  };
  const addTemplate = (templateId: string) => {
    const template = PLOT_TEMPLATES.find((candidate) => candidate.id === templateId);
    if (!template) return;
    const additions = template.signals
      .map((signal) => signal.name)
      .filter((signal) => availableSignalIds.has(signal));
    if (additions.length === 0) return;
    setSelectedSignals((current) => [...new Set([...current, ...additions])]);
    if (mode === "create" && !title.trim()) setTitle(template.name);
  };
  const selectedUnits = [...new Set(selectedSignals
    .map((name) => availableSignals.find((signal) => signal.name === name)?.unit)
    .filter((unit): unit is string => Boolean(unit)))];
  const minimum = yMin.trim() === "" ? Number.NaN : Number(yMin);
  const maximum = yMax.trim() === "" ? Number.NaN : Number(yMax);
  const manualRangeValid = yAxisMode === "auto"
    || (Number.isFinite(minimum) && Number.isFinite(maximum) && minimum < maximum);
  const valid = selectedSignals.length > 0 && manualRangeValid;
  const toggleSignal = (signal: string) => setSelectedSignals((current) => current.includes(signal)
    ? current.filter((name) => name !== signal)
    : [...current, signal]);

  const submit = (event: FormEvent) => {
    event.preventDefault();
    if (!valid) return;
    const fallbackTitle = formatSignalName(selectedSignals[0]);
    onApply({
      title: title.trim() || fallbackTitle,
      signals: selectedSignals.map((name) => initialPlot?.signals.find((signal) => signal.name === name) ?? { name }),
      yAxis: yAxisMode === "manual"
        ? { mode: "manual", min: minimum, max: maximum }
        : { mode: "auto" },
      showLegend,
    });
  };

  return <Dialog
    className="console-dialog plot-settings-dialog"
    icon={mode === "edit" ? IconNames.EDIT : IconNames.ADD}
    isOpen={isOpen}
    onClose={onCancel}
    title={mode === "create" ? "Add plot" : "Plot settings"}
  >
    <form onSubmit={submit}>
      <DialogBody>
        <FormGroup label="Plot title" labelFor="plot-settings-title" helperText="Leave blank to use the first signal name.">
          <InputGroup
            id="plot-settings-title"
            aria-label="Plot title"
            autoFocus
            value={title}
            onChange={(event) => setTitle(event.currentTarget.value)}
          />
        </FormGroup>
        <FormGroup label="Signals" helperText={`${selectedSignals.length} selected`}>
          <div className="plot-template-picker">
            <span>Add from template</span>
            <HTMLSelect
              aria-label="Add from plot template"
              fill
              value=""
              onChange={(event) => addTemplate(event.currentTarget.value)}
            >
              <option value="">Choose a plot template…</option>
              {templateGroups.map(([category, templates]) => <optgroup key={category} label={category}>
                {templates.map((template) => {
                  const available = templateAvailability(template.id);
                  return <option key={template.id} value={template.id} disabled={available === 0}>
                    {template.name} · {available}/{template.signals.length} signals
                  </option>;
                })}
              </optgroup>)}
            </HTMLSelect>
          </div>
          <InputGroup
            aria-label="Search signals"
            leftIcon={IconNames.SEARCH}
            placeholder="Search names, ids, symbols, or units"
            value={signalQuery}
            onChange={(event) => setSignalQuery(event.currentTarget.value)}
          />
          <div className="plot-settings-signals" role="group" aria-label="Available signals">
            {filteredSignalGroups.length === 0
              ? <div className="plot-settings-empty">No matching signals.</div>
              : filteredSignalGroups.map((group) => <section className="signal-picker-category" key={group.category}>
                <header>{group.category}</header>
                {group.subcategories.map((subcategory) => <div className="signal-picker-subcategory" key={subcategory.name}>
                  <div className="signal-picker-subcategory-title">{subcategory.name}</div>
                  {subcategory.signals.map((signal) => <Checkbox
                    className="plot-settings-signal-row"
                    key={signal.id}
                    checked={selectedSignals.includes(signal.id)}
                    labelElement={<span className="signal-picker-row-content">
                      <span className="signal-picker-display-name">{signal.displayName}</span>
                      <SignalSymbol latex={signal.symbolLatex} label={signal.symbol} />
                      <code>{signal.id}</code>
                      <small>{signal.unit}</small>
                    </span>}
                    onChange={() => toggleSignal(signal.id)}
                  />)}
                </div>)}
              </section>)}
          </div>
        </FormGroup>
        {selectedUnits.length > 1 && <Callout compact intent={Intent.WARNING}>
          Selected signals use different units. They will share one mixed-unit Y axis.
        </Callout>}
        <FormGroup label="Y axis">
          <RadioGroup
            inline
            name="plot-y-axis-mode"
            onChange={(event) => setYAxisMode(event.currentTarget.value as "auto" | "manual")}
            selectedValue={yAxisMode}
          >
            <Radio label="Auto" value="auto" />
            <Radio label="Manual" value="manual" />
          </RadioGroup>
          <div className="plot-settings-range">
            <FormGroup label="Minimum" labelFor="plot-settings-y-min">
              <NumericInput
                id="plot-settings-y-min"
                aria-label="Y-axis minimum"
                buttonPosition="none"
                disabled={yAxisMode !== "manual"}
                fill
                value={yMin}
                onValueChange={(_, value) => setYMin(value)}
              />
            </FormGroup>
            <FormGroup label="Maximum" labelFor="plot-settings-y-max">
              <NumericInput
                id="plot-settings-y-max"
                aria-label="Y-axis maximum"
                buttonPosition="none"
                disabled={yAxisMode !== "manual"}
                fill
                value={yMax}
                onValueChange={(_, value) => setYMax(value)}
              />
            </FormGroup>
          </div>
          {yAxisMode === "manual" && !manualRangeValid && <Callout compact intent={Intent.DANGER} role="alert">
            Enter a numeric Y range with minimum less than maximum.
          </Callout>}
        </FormGroup>
        <Checkbox checked={showLegend} label="Show legend" onChange={(event) => setShowLegend(event.currentTarget.checked)} />
      </DialogBody>
      <DialogFooter actions={<>
        <Button type="button" onClick={onCancel}>Cancel</Button>
        <Button intent={Intent.PRIMARY} type="submit" disabled={!valid}>
          {mode === "create" ? "Add Plot" : "Apply"}
        </Button>
      </>} />
    </form>
  </Dialog>;
}
