import { Button, Tooltip } from "@blueprintjs/core";
import { IconNames } from "@blueprintjs/icons";
import { useMemo, type DragEvent } from "react";
import { TimeSeriesChart, type ChartSeries } from "../../components/TimeSeriesChart";
import type { SignalMetadata } from "../../types/api";
import type { HorizontalReferenceLine, PlotInstance, ResolvedPlotSeries, TimelineState, WorkspaceTelemetry } from "./plotTypes";
import { formatSignalName } from "./signalLabels";

const SIGNAL_COLORS = ["#f2bd52", "#21c8b5", "#5b8def", "#d56ef4", "#ff7373", "#8abbff", "#72ca9b"];

function signalColor(signal: string): string {
  let hash = 0;
  for (const character of signal) hash = ((hash << 5) - hash + character.charCodeAt(0)) | 0;
  return SIGNAL_COLORS[Math.abs(hash) % SIGNAL_COLORS.length];
}

function unwrapAngles(values: number[], unit?: string): number[] {
  if (values.length < 2) return values;
  const period = unit === "deg" ? 360 : 2 * Math.PI;
  const halfPeriod = period / 2;
  const result = [values[0]];
  for (let index = 1; index < values.length; index += 1) {
    let value = values[index];
    const previous = result[index - 1];
    while (value - previous > halfPeriod) value -= period;
    while (value - previous < -halfPeriod) value += period;
    result.push(value);
  }
  return result;
}

function alignAngularSeries(values: number[], unit: string | undefined, anchor: number): number[] {
  if (values.length === 0) return values;
  const period = unit === "deg" ? 360 : 2 * Math.PI;
  const offset = Math.round((anchor - values[0]) / period) * period;
  return offset === 0 ? values : values.map((value) => value + offset);
}

interface Props {
  plot: PlotInstance;
  telemetry: WorkspaceTelemetry | null;
  availableSignals: SignalMetadata[];
  timeline: TimelineState;
  displayMode?: "workspace" | "dialog";
  onConfigure?: () => void;
  onVisibleRangeChange: (start: number, end: number) => void;
  onCursorTimeChange: (time: number | null) => void;
  onMaximize?: () => void;
  onClose?: () => void;
  dragging?: boolean;
  onDragStart?: (event: DragEvent<HTMLButtonElement>) => void;
  onDragEnd?: () => void;
  resolveSignal?: (signal: string, telemetry: WorkspaceTelemetry, variant?: string) => ResolvedPlotSeries[];
  hasSignal?: (signal: string, telemetry: WorkspaceTelemetry, variant?: string) => boolean;
  acceptanceBandDeg?: number;
  horizontalReferenceLines?: HorizontalReferenceLine[];
}

export function PlotPanel({
  plot,
  telemetry,
  availableSignals,
  timeline,
  displayMode = "workspace",
  onConfigure,
  onVisibleRangeChange,
  onCursorTimeChange,
  onMaximize,
  onClose,
  dragging = false,
  onDragStart,
  onDragEnd,
  resolveSignal,
  hasSignal,
  acceptanceBandDeg,
  horizontalReferenceLines,
}: Props) {
  const missingRequiredSignals = telemetry
    ? plot.signals.filter((signal) => !signal.optional && !(hasSignal
      ? hasSignal(signal.name, telemetry, plot.presentationVariant)
      : telemetry.series[signal.name]))
    : [];
  const chartSeries = useMemo<ChartSeries[]>(() => {
    let angularAnchor: number | undefined;
    return plot.signals.flatMap((signal) => {
      if (!telemetry) return [];
      const resolved = resolveSignal
        ? resolveSignal(signal.name, telemetry, plot.presentationVariant)
        : telemetry.series[signal.name] ? [{
          key: signal.name,
          name: formatSignalName(signal.name, availableSignals.find((item) => item.name === signal.name)),
          time: telemetry.time,
          values: telemetry.series[signal.name],
          unit: telemetry.units[signal.name],
        }] : [];
      return resolved.flatMap((item) => {
        let values = plot.angularAware ? unwrapAngles(item.values, item.unit) : item.values;
        if (plot.angularAware && values.length > 0) {
          angularAnchor ??= values[0];
          values = alignAngularSeries(values, item.unit, angularAnchor);
        }
        const color = signalColor(item.key);
        const measured: ChartSeries = {
          name: item.name,
          time: item.time,
          values,
          color,
          dashed: signal.name.endsWith(".commanded") || signal.name === "commanded" || signal.name.startsWith("commanded_"),
        };
        if (
          acceptanceBandDeg == null
          || plot.acceptanceBandSignal !== signal.name
        ) return [measured];
        const halfWidth = item.unit === "rad"
          ? acceptanceBandDeg * Math.PI / 180
          : acceptanceBandDeg;
        return [
          measured,
          { ...measured, name: `${item.name} + acceptance`, values: values.map((value) => value + halfWidth), dashed: true },
          { ...measured, name: `${item.name} - acceptance`, values: values.map((value) => value - halfWidth), dashed: true },
        ];
      });
    });
  }, [acceptanceBandDeg, availableSignals, plot.acceptanceBandSignal, plot.angularAware, plot.presentationVariant, plot.signals, resolveSignal, telemetry]);
  const fullTimeRange = useMemo<[number, number] | undefined>(() => telemetry && telemetry.time.length > 0
    ? [telemetry.time[0], telemetry.time[telemetry.time.length - 1]]
    : undefined, [telemetry]);
  const units = [...new Set(plot.signals.map((signal) =>
    telemetry?.units[signal.name] ?? availableSignals.find((item) => item.name === signal.name)?.unit,
  ).filter(Boolean))];
  const unit = units.length === 1 ? units[0] ?? "" : units.length > 1 ? "mixed" : "";
  return <section className={`plot-panel${displayMode === "dialog" ? " plot-panel-dialog" : ""}${dragging ? " plot-panel-dragging" : ""}`} data-slot={plot.slot}>
    <header className="plot-panel-header">
      <div className="plot-panel-title">
        <span>{plot.title}</span>
        {unit && <small>{unit}</small>}
      </div>
      {displayMode === "workspace" && <div className="plot-panel-actions">
        <Tooltip content="Drag to rearrange">
          <Button
            aria-label={`Move ${plot.title}`}
            className="plot-drag-handle"
            draggable
            icon={IconNames.DRAG_HANDLE_VERTICAL}
            minimal
            small
            onDragStart={onDragStart}
            onDragEnd={onDragEnd}
          />
        </Tooltip>
        <Tooltip content="Edit plot">
          <Button aria-label={`Edit ${plot.title}`} icon={IconNames.EDIT} minimal small onClick={onConfigure} />
        </Tooltip>
        <Tooltip content="Maximize plot">
          <Button aria-label={`Maximize ${plot.title}`} icon={IconNames.MAXIMIZE} minimal small onClick={onMaximize} />
        </Tooltip>
        <Tooltip content="Remove plot">
          <Button aria-label={`Remove ${plot.title}`} icon={IconNames.CROSS} minimal small onClick={onClose} />
        </Tooltip>
      </div>}
    </header>
    {missingRequiredSignals.length > 0 && <div className="plot-signal-warning" role="status">
      Required signal unavailable: <code>{missingRequiredSignals.map((signal) => signal.name).join(", ")}</code>
    </div>}
    {chartSeries.length > 0
      ? <TimeSeriesChart
        title={plot.title}
        unit={unit}
        series={chartSeries}
        showHeader={false}
        timeline={timeline}
        onVisibleRangeChange={onVisibleRangeChange}
        onCursorTimeChange={onCursorTimeChange}
        yAxisMin={plot.yAxis.mode === "manual" ? plot.yAxis.min : undefined}
        yAxisMax={plot.yAxis.mode === "manual" ? plot.yAxis.max : undefined}
        fullTimeRange={fullTimeRange}
        showLegend={plot.showLegend}
        annotations={horizontalReferenceLines?.length ? { horizontalLines: horizontalReferenceLines } : undefined}
      />
      : <div className="plot-panel-empty">Select one or more signals.</div>}
  </section>;
}
