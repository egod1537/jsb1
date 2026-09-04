import { api } from "../../api/client";
import type { SignalMetadata } from "../../types/api";
import { formatSignalName } from "./signalLabels";
import { loadRunSignalUnion } from "./runSignalDataSource";
import type { PlotDataSource, ResolvedPlotSeries, WorkspaceTelemetry } from "./plotTypes";

export type IntraRunView = string;

function arraysEqual(left: number[] | undefined, right: number[] | undefined): boolean {
  return left != null && right != null
    && left.length === right.length
    && left.every((value, index) => value === right[index]);
}

export function createRunVariantDataSource(
  runId: number,
  availableVariants: string[],
  view: IntraRunView,
): PlotDataSource {
  const selected = view === "overlay"
    ? availableVariants
    : availableVariants.includes(view) ? [view] : availableVariants.slice(0, 1);
  const seriesKey = (variant: string, signal: string) => `${variant}/${signal}`;
  const metadataByName = new Map<string, SignalMetadata>();
  let metadataRequest: Promise<SignalMetadata[]> | null = null;
  const loadMetadata = () => {
    metadataRequest ??= api.availableSignals(runId).then(({ signals }) => {
      signals.forEach((signal) => metadataByName.set(signal.name, signal));
      return signals;
    });
    return metadataRequest;
  };
  const label = (signal: string) => formatSignalName(signal, metadataByName.get(signal));
  return {
    key: `run-${runId}-${view}-${selected.join("-")}`,
    async listSignals(): Promise<SignalMetadata[]> {
      return loadMetadata();
    },
    async loadSignals(signals: string[]): Promise<WorkspaceTelemetry> {
      const [, responses] = await Promise.all([
        loadMetadata(),
        Promise.all(selected.map((variant) => loadRunSignalUnion(runId, signals, 4000, variant))),
      ]);
      const series: Record<string, number[]> = {};
      const seriesTimes: Record<string, number[]> = {};
      const units: Record<string, string> = {};
      responses.forEach((response, index) => {
        const variant = selected[index];
        for (const [signal, values] of Object.entries(response.series)) {
          const key = seriesKey(variant, signal);
          series[key] = values;
          seriesTimes[key] = response.time;
          units[signal] ??= response.units[signal] ?? "raw";
        }
      });
      return {
        time: responses[0]?.time ?? [],
        series,
        seriesTimes,
        units,
        source_points: Math.max(0, ...responses.map((item) => item.source_points)),
        returned_points: Math.max(0, ...responses.map((item) => item.returned_points)),
      };
    },
    resolveSignal(signal, telemetry): ResolvedPlotSeries[] {
      const resolved = selected.flatMap((variant) => {
        const key = seriesKey(variant, signal);
        const values = telemetry.series[key];
        if (!values) return [];
        return [{
          key,
          name: view === "overlay"
            ? `${variant} / ${label(signal)}`
            : label(signal),
          time: telemetry.seriesTimes?.[key] ?? telemetry.time,
          values,
          unit: telemetry.units[signal],
        }];
      });
      if (view === "overlay" && signal.startsWith("commanded_") && resolved.length > 1) {
        const first = resolved[0];
        const identical = resolved.slice(1).every((item) =>
          arraysEqual(first.values, item.values) && arraysEqual(first.time, item.time));
        if (identical) return [{ ...first, name: label(signal) }];
      }
      return resolved;
    },
    hasSignal(signal, telemetry): boolean {
      return selected.every((variant) => Boolean(telemetry.series[seriesKey(variant, signal)]));
    },
  };
}
