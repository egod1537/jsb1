import { api } from "../../api/client";
import type { SignalMetadata } from "../../types/api";
import { formatSignalName } from "./signalLabels";
import { loadRunSignalUnion } from "./runSignalDataSource";
import type { PlotDataSource, ResolvedPlotSeries, WorkspaceTelemetry } from "./plotTypes";

export interface ComparedRunData {
  runId: number;
  label: string;
  variant?: string;
}

function arraysEqual(left: number[] | undefined, right: number[] | undefined): boolean {
  return left != null && right != null
    && left.length === right.length
    && left.every((value, index) => value === right[index]);
}

export function createRunComparisonDataSource(runs: ComparedRunData[]): PlotDataSource {
  const labels = runs.map((run) => run.label);
  const runByLabel = new Map(runs.map((run) => [run.label, run]));
  const seriesKey = (runId: number, signal: string) => `run-${runId}/${signal}`;
  const metadataByName = new Map<string, SignalMetadata>();
  let metadataRequest: Promise<SignalMetadata[]> | null = null;
  const loadMetadata = () => {
    metadataRequest ??= Promise.all(runs.map((run) => api.availableSignals(run.runId))).then((responses) => {
      for (const response of responses) {
        for (const signal of response.signals) metadataByName.set(signal.name, signal);
      }
      return [...metadataByName.values()].sort((left, right) => left.name.localeCompare(right.name));
    });
    return metadataRequest;
  };
  const label = (signal: string) => formatSignalName(signal, metadataByName.get(signal));
  return {
    key: `runs-compare-${runs.map((run) => run.runId).join("-")}`,
    variants: labels,
    async listSignals(): Promise<SignalMetadata[]> {
      return loadMetadata();
    },
    async loadSignals(signals: string[]): Promise<WorkspaceTelemetry> {
      const [, responses] = await Promise.all([
        loadMetadata(),
        Promise.all(runs.map((run) => loadRunSignalUnion(run.runId, signals, 4000, run.variant))),
      ]);
      const series: Record<string, number[]> = {};
      const seriesTimes: Record<string, number[]> = {};
      const units: Record<string, string> = {};
      responses.forEach((response, index) => {
        const run = runs[index];
        for (const [signal, values] of Object.entries(response.series)) {
          const key = seriesKey(run.runId, signal);
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
        source_points: Math.max(0, ...responses.map((response) => response.source_points)),
        returned_points: Math.max(0, ...responses.map((response) => response.returned_points)),
      };
    },
    resolveSignal(signal, telemetry, selectedLabel): ResolvedPlotSeries[] {
      const selectedRuns = selectedLabel
        ? [runByLabel.get(selectedLabel)].filter((run): run is ComparedRunData => run != null)
        : runs;
      const resolved = selectedRuns.flatMap((run) => {
        const key = seriesKey(run.runId, signal);
        const values = telemetry.series[key];
        if (!values) return [];
        return [{
          key,
          name: selectedLabel ? label(signal) : `${run.label} / ${label(signal)}`,
          time: telemetry.seriesTimes?.[key] ?? telemetry.time,
          values,
          unit: telemetry.units[signal],
        }];
      });
      if (!selectedLabel && signal.startsWith("commanded_") && resolved.length === runs.length) {
        const first = resolved[0];
        const identical = resolved.slice(1).every((series) =>
          arraysEqual(first.values, series.values) && arraysEqual(first.time, series.time));
        if (identical) return [{ ...first, name: label(signal) }];
      }
      return resolved;
    },
    hasSignal(signal, telemetry, selectedLabel): boolean {
      const requiredRuns = selectedLabel
        ? [runByLabel.get(selectedLabel)].filter((run): run is ComparedRunData => run != null)
        : runs;
      return requiredRuns.every((run) => Boolean(telemetry.series[seriesKey(run.runId, signal)]));
    },
  };
}
