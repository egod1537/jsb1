import { useEffect, useRef } from "react";
import * as echarts from "echarts/core";
import { LineChart } from "echarts/charts";
import {
  DataZoomComponent,
  GridComponent,
  LegendComponent,
  TooltipComponent,
} from "echarts/components";
import { CanvasRenderer } from "echarts/renderers";

echarts.use([
  LineChart,
  DataZoomComponent,
  GridComponent,
  LegendComponent,
  TooltipComponent,
  CanvasRenderer,
]);

export interface ChartSeries {
  name: string;
  time: number[];
  values: number[];
  color?: string;
  dashed?: boolean;
}

interface Props {
  title: string;
  unit: string;
  series: ChartSeries[];
  group?: string;
}

export function TimeSeriesChart({ title, unit, series, group }: Props) {
  const element = useRef<HTMLDivElement>(null);
  let start = Number.POSITIVE_INFINITY;
  let end = Number.NEGATIVE_INFINITY;
  for (const item of series) {
    for (const time of item.time) {
      start = Math.min(start, time);
      end = Math.max(end, time);
    }
  }
  const range = Number.isFinite(start) && Number.isFinite(end)
    ? `${start.toFixed(1)}–${end.toFixed(1)} s`
    : "No samples";

  useEffect(() => {
    if (!element.current) return;
    const chart = echarts.init(element.current, undefined, { renderer: "canvas" });
    if (group) {
      chart.group = group;
      echarts.connect(group);
    }
    chart.setOption({
      animation: false,
      color: series.map((item) => item.color).filter((color): color is string => Boolean(color)),
      legend: {
        right: 16,
        top: 8,
        textStyle: { color: "#abb3bf" },
        icon: "roundRect",
      },
      grid: { left: 62, right: 22, top: 38, bottom: 62 },
      tooltip: {
        trigger: "axis",
        backgroundColor: "rgba(37, 42, 49, 0.96)",
        borderColor: "#5f6b7c",
        textStyle: { color: "#f6f7f9" },
        valueFormatter: (value: unknown) => `${Number(value).toFixed(3)} ${unit}`,
      },
      xAxis: {
        type: "value",
        name: "time (s)",
        nameLocation: "middle",
        nameGap: 28,
        axisLine: { lineStyle: { color: "#5f6b7c" } },
        axisLabel: { color: "#abb3bf" },
        splitLine: { lineStyle: { color: "#383e47" } },
      },
      yAxis: {
        type: "value",
        name: unit,
        axisLabel: { color: "#abb3bf" },
        splitLine: { lineStyle: { color: "#383e47" } },
      },
      dataZoom: [
        { type: "inside", filterMode: "none" },
        {
          type: "slider",
          height: 18,
          bottom: 10,
          borderColor: "#5f6b7c",
          fillerColor: "rgba(45, 114, 210, 0.2)",
          handleStyle: { color: "#4c90f0" },
          textStyle: { color: "#abb3bf" },
        },
      ],
      series: series.map((item) => ({
        name: item.name,
        type: "line",
        showSymbol: false,
        sampling: "lttb",
        lineStyle: { width: 1.8, type: item.dashed ? "dashed" : "solid" },
        data: item.time.map((time, index) => [time, item.values[index]]),
      })),
    });
    const resize = new ResizeObserver(() => chart.resize());
    resize.observe(element.current);
    return () => {
      resize.disconnect();
      chart.dispose();
    };
  }, [group, series, unit]);

  return <section className="chart-panel">
    <header className="panel-header chart-panel-header">
      <div><span className="panel-title">{title}</span><span className="chart-unit">{unit}</span></div>
      <span className="chart-range">{range}</span>
    </header>
    <div className="chart" ref={element} aria-label={`${title} chart`} />
  </section>;
}
