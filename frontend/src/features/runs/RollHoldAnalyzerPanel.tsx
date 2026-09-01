import { Button, ButtonGroup, Callout, Dialog, DialogBody, Spinner, Tag } from "@blueprintjs/core";
import { IconNames } from "@blueprintjs/icons";
import { useEffect, useMemo, useState } from "react";
import { api } from "../../api/client";
import { TimeSeriesChart, type ChartAnnotations, type ChartSeries } from "../../components/TimeSeriesChart";
import type { RollHoldAnalysis, RollHoldAnalysisVariants, SignalResponse } from "../../types/api";
import type { TimelineState } from "../plots/plotTypes";
import { loadRunSignalUnion } from "../plots/runSignalDataSource";

interface Props {
  runId: number;
  timeline: TimelineState;
  onVisibleRangeChange: (start: number, end: number) => void;
  onCursorTimeChange: (time: number | null) => void;
  onFocusRange: (start: number, end: number) => void;
  onViewParameters?: () => void;
}

const SIGNALS = ["commanded_roll", "roll", "commanded_roll_rate", "roll_rate", "aileron"];
const SUMMARY_METRICS = [
  ["settling_time_s", "Settling"],
  ["overshoot_deg", "Overshoot"],
  ["rms_tracking_error_deg", "RMS error"],
  ["steady_state_error_deg", "Steady error"],
  ["oscillation_count", "Oscillation"],
] as const;
const TARGET_GROUPS = [
  ["tracking", "Tracking"],
  ["dynamics", "Dynamics"],
  ["control", "Control"],
] as const;

type AnalyzerMode = "primary" | "baseline" | "compare";
type AnalyzerPlotId = "roll_tracking" | "roll_rate" | "aileron";
type DiagnosticFocus = { primary: AnalyzerPlotId; secondary?: AnalyzerPlotId; annotationIds: string[] };

export const ROLL_HOLD_DIAGNOSTICS: Record<string, DiagnosticFocus> = {
  rise_time: { primary: "roll_tracking", annotationIds: ["rise_10", "rise_90", "rise_interval"] },
  settling_time: { primary: "roll_tracking", annotationIds: ["settled", "settling_band", "settling_region"] },
  overshoot: { primary: "roll_tracking", annotationIds: ["peak_roll", "command_reference"] },
  steady_state_error: { primary: "roll_tracking", annotationIds: ["steady_state_error_region", "steady_state_mean", "command_reference"] },
  rms_tracking_error: { primary: "roll_tracking", annotationIds: ["tracking_error_region"] },
  oscillation: { primary: "roll_tracking", secondary: "roll_rate", annotationIds: ["oscillation_region"] },
  oscillation_count: { primary: "roll_tracking", secondary: "roll_rate", annotationIds: ["oscillation_region"] },
  residual_oscillation: { primary: "roll_tracking", annotationIds: ["residual_min", "residual_max", "steady_state_region"] },
  dominant_period: { primary: "roll_tracking", secondary: "roll_rate", annotationIds: ["oscillation_region"] },
  peak_aileron: { primary: "aileron", annotationIds: ["peak_aileron"] },
  rms_aileron: { primary: "aileron", annotationIds: ["control_response_region"] },
  saturation_time: { primary: "aileron", annotationIds: ["saturation_limits", "saturation_intervals", "aileron_saturation"] },
};
const DEFAULT_PLOT_ORDER: AnalyzerPlotId[] = ["roll_tracking", "roll_rate", "aileron"];
const COLORS: Record<string, string> = { primary: "#72ca9b", baseline: "#f2bd52" };

function displayUnit(unit: string): string {
  return unit === "deg" ? "°" : unit === "deg/s" ? "°/s" : unit;
}

function formattedValue(value: number | null, unit: string): string {
  if (value == null) return "N/A";
  const digits = unit === "cycles" ? 0 : 3;
  return `${value.toFixed(digits)}${unit ? ` ${displayUnit(unit)}` : ""}`;
}

function metricValue(result: RollHoldAnalysis, name: string): string {
  const value = result.metrics[name];
  if (value == null) return name === "settling_time_s" ? "Not settled" : "—";
  if (typeof value === "boolean") return value ? "Detected" : "Not detected";
  return formattedValue(value, result.metric_units[name] ?? "");
}

function chartSeries(
  telemetry: SignalResponse | undefined,
  signal: string,
  label: string,
  color: string,
  dashed = false,
): ChartSeries | null {
  const values = telemetry?.series[signal];
  return telemetry && values ? { name: label, time: telemetry.time, values, color, dashed } : null;
}

function availableSeries(items: Array<ChartSeries | null>): ChartSeries[] {
  return items.filter((item): item is ChartSeries => item !== null);
}

function arraysEqual(left?: number[], right?: number[]): boolean {
  return left != null && right != null && left.length === right.length
    && left.every((value, index) => value === right[index]);
}

async function loadAnalyzerTelemetry(runId: number, variant: string): Promise<SignalResponse> {
  const available = await api.availableSignals(runId);
  const availableNames = new Set(available.variants?.[variant] ?? available.signals.map((item) => item.name));
  const requested = SIGNALS.filter((signal) => availableNames.has(signal));
  if (requested.length === 0) return { time: [], series: {}, units: {}, source_points: 0, returned_points: 0 };
  return loadRunSignalUnion(runId, requested, 4000, variant);
}

function normalizeAnalysis(payload: RollHoldAnalysisVariants | RollHoldAnalysis): RollHoldAnalysisVariants {
  if ("variants" in payload) return payload;
  return { variants: { primary: payload } };
}

function statusIntent(status: RollHoldAnalysis["checks"][number]["status"]) {
  return status === "pass" ? "success"
    : status === "warn" ? "warning"
      : status === "fail" ? "danger"
        : "none";
}

interface TargetPerformanceNavigatorProps {
  analyses: Record<string, RollHoldAnalysis>;
  mode: AnalyzerMode;
  result: RollHoldAnalysis;
  selectedMetricId: string | null;
  onClearFocus: () => void;
  onSelectMetric: (check: RollHoldAnalysis["checks"][number]) => void;
}

function TargetPerformanceNavigator(props: TargetPerformanceNavigatorProps) {
  const { analyses, mode, result, selectedMetricId, onClearFocus, onSelectMetric } = props;
  const checks = mode === "compare" ? analyses.primary?.checks ?? result.checks : result.checks;

  return <aside className="analyzer-targets" aria-label="Target Performance">
    <header>
      <span>Target Performance</span>
      {selectedMetricId && <Button
        aria-label="Clear Focus"
        icon={IconNames.CROSS}
        minimal
        onClick={onClearFocus}
        small
        title="Clear Focus"
      />}
    </header>
    <small className="analyzer-targets-note">JSB1 diagnostic targets, not certification limits.</small>
    <nav className="analyzer-target-navigator" aria-label="Target performance metrics">
      {TARGET_GROUPS.map(([category, label]) => <section className="analyzer-target-group" key={category}>
        <h3>{label}</h3>
        {checks.filter((check) => check.category === category).map((check) => {
          const primary = analyses.primary?.checks.find((item) => item.id === check.id);
          const baseline = analyses.baseline?.checks.find((item) => item.id === check.id);
          const selectedCheck = mode === "compare" ? primary ?? baseline ?? check : check;
          const delta = primary?.actual != null && baseline?.actual != null
            ? primary.actual - baseline.actual
            : null;
          return <button
            aria-label={check.label}
            className={`analyzer-target-item${selectedMetricId === check.id ? " is-selected" : ""}`}
            key={check.id}
            onClick={() => onSelectMetric(selectedCheck)}
            type="button"
          >
            <span className="analyzer-target-label">{check.label}</span>
            {mode === "compare" ? <span className="analyzer-target-compare-values">
              <span><small>Primary</small><strong>{formattedValue(primary?.actual ?? null, check.unit)}</strong></span>
              <span><small>Baseline</small><strong>{formattedValue(baseline?.actual ?? null, check.unit)}</strong></span>
              <span><small>Δ</small><strong>{delta == null ? "—" : formattedValue(delta, check.unit)}</strong></span>
            </span> : <>
              <span className="analyzer-target-result">
                <strong>{formattedValue(check.actual, check.unit)}</strong>
                <Tag minimal intent={statusIntent(check.status)}>{check.status === "unavailable" ? "N/A" : check.status.toUpperCase()}</Tag>
              </span>
              <small className="analyzer-target-source">Target {formattedValue(check.target, check.unit)} · {check.target_source === "default" ? "diagnostic default" : check.target_source}</small>
            </>}
          </button>;
        })}
      </section>)}
    </nav>
  </aside>;
}

export function RollHoldAnalyzerPanel(props: Props) {
  const { runId, timeline, onVisibleRangeChange, onCursorTimeChange, onFocusRange, onViewParameters } = props;
  const [open, setOpen] = useState(false);
  const [response, setResponse] = useState<RollHoldAnalysisVariants | null>(null);
  const [telemetry, setTelemetry] = useState<Record<string, SignalResponse>>({});
  const [mode, setMode] = useState<AnalyzerMode>("compare");
  const [error, setError] = useState<string | null>(null);
  const [selectedMetricId, setSelectedMetricId] = useState<string | null>(null);

  useEffect(() => {
    if (!open || response || error) return;
    let active = true;
    api.rollHoldAnalysis(runId).then(async (raw) => {
      const analysis = normalizeAnalysis(raw);
      const variants = Object.keys(analysis.variants);
      const loaded = await Promise.all(variants.map(async (variant) => [variant, await loadAnalyzerTelemetry(runId, variant)] as const));
      if (!active) return;
      setResponse(analysis);
      setTelemetry(Object.fromEntries(loaded));
      if (variants.length === 1) setMode(variants[0] as AnalyzerMode);
    }).catch((reason: Error) => { if (active) setError(reason.message); });
    return () => { active = false; };
  }, [error, open, response, runId]);

  const analyses = response?.variants ?? {};
  const variants = Object.keys(analyses);
  const activeVariants = mode === "compare" ? variants : variants.includes(mode) ? [mode] : variants.slice(0, 1);
  const annotationVariant = activeVariants.includes("primary") ? "primary" : activeVariants[0];
  const result = annotationVariant ? analyses[annotationVariant] : undefined;
  const diagnosticFocus = selectedMetricId ? ROLL_HOLD_DIAGNOSTICS[selectedMetricId] : undefined;
  const focused = useMemo(() => new Set(diagnosticFocus?.annotationIds ?? []), [diagnosticFocus]);
  const plotOrder = useMemo(() => diagnosticFocus
    ? [diagnosticFocus.primary, diagnosticFocus.secondary, ...DEFAULT_PLOT_ORDER]
      .filter((value): value is AnalyzerPlotId => Boolean(value))
      .filter((value, index, items) => items.indexOf(value) === index)
    : DEFAULT_PLOT_ORDER, [diagnosticFocus]);
  const firstTelemetry = annotationVariant ? telemetry[annotationVariant] : undefined;
  const fullTimeRange: [number, number] = firstTelemetry?.time.length
    ? [firstTelemetry.time[0], firstTelemetry.time[firstTelemetry.time.length - 1]]
    : [0, 0];

  const rollAnnotations = useMemo<ChartAnnotations | undefined>(() => {
    if (!result) return undefined;
    const { markers, regions, parameters } = result;
    const band = Number(parameters.settling_band_deg);
    const target = markers.command?.value;
    const riseStart = markers.rise_10?.time_sec;
    const riseEnd = markers.rise_90?.time_sec;
    const settlingRegion = regions.settling ?? (focused.has("settling_region") ? regions.response : undefined);
    return {
      verticalLines: [
        ...(markers.command ? [{ time: markers.command.time_sec, label: "Command" }] : []),
        ...(markers.settled ? [{ time: markers.settled.time_sec, label: "Settled", emphasized: focused.has("settled") }] : []),
        ...(markers.rise_10 ? [{ time: markers.rise_10.time_sec, label: "Rise 10%", emphasized: focused.has("rise_10") }] : []),
        ...(markers.rise_90 ? [{ time: markers.rise_90.time_sec, label: "Rise 90%", emphasized: focused.has("rise_90") }] : []),
      ],
      horizontalLines: target != null && Number.isFinite(target) && focused.has("command_reference")
        ? [{ value: target, label: "Commanded roll", emphasized: true }]
        : [],
      verticalAreas: [
        ...(settlingRegion ? [{ start: settlingRegion.start_sec, end: settlingRegion.end_sec, label: "Settling measurement", emphasized: focused.has("settling_region") }] : []),
        ...(regions.steady_state ? [{ start: regions.steady_state.start_sec, end: regions.steady_state.end_sec, label: "Steady-state region", emphasized: focused.has("steady_state_region") }] : []),
        ...(riseStart != null && riseEnd != null ? [{ start: riseStart, end: riseEnd, label: "Rise-time interval", emphasized: focused.has("rise_interval") }] : []),
        ...(focused.has("steady_state_error_region") && regions.steady_state_error ? [{ start: regions.steady_state_error.start_sec, end: regions.steady_state_error.end_sec, label: "Steady-state error window", emphasized: true }] : []),
        ...(focused.has("tracking_error_region") && regions.response ? [{ start: regions.response.start_sec, end: regions.response.end_sec, label: "RMS tracking window", emphasized: true }] : []),
        ...(focused.has("oscillation_region") && regions.response ? [{ start: regions.response.start_sec, end: regions.response.end_sec, label: "Oscillation analysis window", emphasized: true }] : []),
      ],
      horizontalBands: target != null && Number.isFinite(target) && Number.isFinite(band) && band > 0
        ? [{ minimum: target - band, maximum: target + band, label: `commanded ±${band}° band`, emphasized: focused.has("settling_band") }]
        : [],
      points: ["rise_10", "rise_90", "peak_roll", "steady_state_mean", "residual_min", "residual_max"].flatMap((id) => {
        const marker = markers[id];
        const labels: Record<string, string> = {
          rise_10: "Rise 10%", rise_90: "Rise 90%", peak_roll: "Overshoot point",
          steady_state_mean: "Steady-state mean",
          residual_min: "Residual min", residual_max: "Residual max",
        };
        return marker?.value == null ? [] : [{ time: marker.time_sec, value: marker.value, label: labels[id] ?? marker.label, emphasized: focused.has(id) }];
      }),
    };
  }, [focused, result]);

  const markerAnnotations = (id: string): ChartAnnotations | undefined => {
    const marker = result?.markers[id];
    return marker?.value == null ? undefined : { points: [{ time: marker.time_sec, value: marker.value, label: marker.label, emphasized: focused.has(id) }] };
  };
  const rollRateAnnotations: ChartAnnotations = {
    ...(markerAnnotations("peak_roll_rate") ?? {}),
    verticalAreas: focused.has("oscillation_region") && result?.regions.response ? [{
      start: result.regions.response.start_sec,
      end: result.regions.response.end_sec,
      label: "Oscillation analysis window",
      emphasized: true,
    }] : [],
  };
  const aileronAnnotations: ChartAnnotations = {
    horizontalLines: typeof result?.parameters.aileron_limit === "number" ? [
      { value: result.parameters.aileron_limit, label: "+ saturation limit", emphasized: focused.has("saturation_limits") },
      { value: -result.parameters.aileron_limit, label: "− saturation limit", emphasized: focused.has("saturation_limits") },
    ] : [],
    verticalAreas: [
      ...(result?.intervals.aileron_saturation?.map((interval) => ({
        start: interval.start_sec, end: interval.end_sec, label: "Saturation interval", emphasized: focused.has("saturation_intervals"),
      })) ?? []),
      ...(focused.has("control_response_region") && result?.regions.response ? [{
        start: result.regions.response.start_sec,
        end: result.regions.response.end_sec,
        label: "Control RMS window",
        emphasized: true,
      }] : []),
    ],
    points: [...(markerAnnotations("peak_aileron")?.points ?? []), ...(markerAnnotations("aileron_saturation")?.points ?? [])],
  };

  const clearMetricFocus = () => {
    setSelectedMetricId(null);
    if (fullTimeRange[1] > fullTimeRange[0]) onFocusRange(...fullTimeRange);
  };

  const selectMetric = (check: RollHoldAnalysis["checks"][number]) => {
    if (selectedMetricId === check.id) return clearMetricFocus();
    setSelectedMetricId(check.id);
    if (check.start_sec != null && check.end_sec != null) onFocusRange(check.start_sec, check.end_sec);
  };

  const diagnosticSeries = (signal: string, command = false): ChartSeries[] => {
    const series = activeVariants.flatMap((variant) => {
      const label = mode === "compare" ? `${variant} / ${signal.replaceAll("_", " ")}` : signal.replaceAll("_", " ");
      return availableSeries([chartSeries(telemetry[variant], signal, label, command ? "#8abbff" : COLORS[variant] ?? "#c58af9", command)]);
    });
    if (command && mode === "compare" && series.length > 1) {
      const first = series[0];
      if (series.slice(1).every((item) => arraysEqual(first.values, item.values) && arraysEqual(first.time, item.time))) {
        return [{ ...first, name: signal.replaceAll("_", " ") }];
      }
    }
    return series;
  };

  return <>
    <section className="panel roll-hold-analyzer-launcher" aria-label="Roll Hold Analyzer">
      <header className="panel-header"><span className="panel-title">Roll Hold Analyzer</span><span className="panel-count">deterministic</span></header>
      <Button fill icon={IconNames.CHART} onClick={() => setOpen(true)} text="Open Roll Hold Analyzer" />
    </section>
    <Dialog className="roll-hold-analyzer-dialog" icon={IconNames.CHART} isOpen={open} onClose={() => setOpen(false)} title="Roll Hold Analyzer">
      <DialogBody className="roll-hold-analyzer-dialog-body" useOverflowScrollContainer={false}>
        {!response && !error && <div className="analyzer-loading"><Spinner size={18} /><span>Analyzing telemetry</span></div>}
        {error && <Callout intent="warning">{error}</Callout>}
        {response && result && <div className="roll-hold-analyzer-content">
          <div className="analyzer-mode-switch" aria-label="Analyzer view"><span>View</span><ButtonGroup minimal>
            <Button active={mode === "primary"} disabled={!variants.includes("primary")} onClick={() => setMode("primary")} small>Primary</Button>
            <Button active={mode === "baseline"} disabled={!variants.includes("baseline")} onClick={() => setMode("baseline")} small>Baseline</Button>
            <Button active={mode === "compare"} disabled={variants.length < 2} onClick={() => setMode("compare")} small>Compare</Button>
          </ButtonGroup></div>
          <section className="analyzer-summary" aria-label="Roll Hold summary">
            <header><span>Summary</span>{onViewParameters && <Button icon={IconNames.PROPERTIES} minimal onClick={onViewParameters} small>Parameters</Button>}</header>
            <dl>{SUMMARY_METRICS.map(([name, label]) => <div key={name}><dt>{label}</dt><dd>{activeVariants.map((variant) => `${mode === "compare" ? `${variant}: ` : ""}${metricValue(analyses[variant], name)}`).join(" · ")}</dd></div>)}</dl>
            {activeVariants.flatMap((variant) => analyses[variant].missing_signals).length > 0 && <Callout compact intent="warning">Missing signals: {activeVariants.flatMap((variant) => analyses[variant].missing_signals.map((signal) => variants.length > 1 ? `${variant}/${signal}` : signal)).join(", ")}</Callout>}
          </section>
          <div className="roll-hold-analyzer-workspace">
            <TargetPerformanceNavigator
              analyses={analyses}
              mode={mode}
              onClearFocus={clearMetricFocus}
              onSelectMetric={selectMetric}
              result={result}
              selectedMetricId={selectedMetricId}
            />
            <div className="roll-hold-analyzer-main">
              <section className="analyzer-plots" aria-label="Roll Hold plots">
                <header>Plots</header>
                <div className="analyzer-plots-scroll">
                  <div className="roll-hold-analyzer-charts">{plotOrder.map((plotId) => <div key={plotId} className={`analyzer-plot-slot${diagnosticFocus?.primary === plotId ? " is-diagnostic-primary" : diagnosticFocus?.secondary === plotId ? " is-diagnostic-secondary" : ""}`} data-plot-id={plotId} data-diagnostic-role={diagnosticFocus?.primary === plotId ? "primary" : diagnosticFocus?.secondary === plotId ? "secondary" : "none"}>
                    {plotId === "roll_tracking" && <TimeSeriesChart title="Roll Tracking" unit="deg" series={[...diagnosticSeries("commanded_roll", true), ...diagnosticSeries("roll")]} annotations={rollAnnotations} fullTimeRange={fullTimeRange} timeline={timeline} onVisibleRangeChange={onVisibleRangeChange} onCursorTimeChange={onCursorTimeChange} />}
                    {plotId === "roll_rate" && <TimeSeriesChart title="Roll Rate" unit="deg/s" series={[...diagnosticSeries("commanded_roll_rate", true), ...diagnosticSeries("roll_rate")]} annotations={rollRateAnnotations} fullTimeRange={fullTimeRange} timeline={timeline} onVisibleRangeChange={onVisibleRangeChange} onCursorTimeChange={onCursorTimeChange} />}
                    {plotId === "aileron" && <TimeSeriesChart title="Aileron" unit="normalized" series={diagnosticSeries("aileron")} annotations={aileronAnnotations} fullTimeRange={fullTimeRange} timeline={timeline} onVisibleRangeChange={onVisibleRangeChange} onCursorTimeChange={onCursorTimeChange} />}
                  </div>)}</div>
                  <section className="analyzer-assessment" aria-label="Roll Hold assessment"><header>Assessment</header><div>{activeVariants.flatMap((variant) => analyses[variant].assessment.map((item) => <div key={`${variant}-${item.code}`}><Tag minimal intent={item.severity === "warning" ? "warning" : item.severity === "success" ? "success" : "none"}>{mode === "compare" ? variant : item.severity}</Tag><span>{item.message}</span></div>))}</div></section>
                </div>
              </section>
            </div>
          </div>
        </div>}
      </DialogBody>
    </Dialog>
  </>;
}
