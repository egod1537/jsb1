import { useEffect, useRef } from "react";
import * as echarts from "echarts/core";
import { LineChart } from "echarts/charts";
import {
  DataZoomComponent,
  GridComponent,
  LegendComponent,
  TitleComponent,
  TooltipComponent,
} from "echarts/components";
import { CanvasRenderer } from "echarts/renderers";

echarts.use([
  LineChart,
  DataZoomComponent,
  GridComponent,
  LegendComponent,
  TitleComponent,
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
      title: {
        text: title,
        left: 14,
        top: 10,
        textStyle: { color: "#dce7f5", fontSize: 14, fontWeight: 600 },
      },
      legend: {
        right: 16,
        top: 11,
        textStyle: { color: "#8495ac" },
        icon: "roundRect",
      },
      grid: { left: 62, right: 22, top: 54, bottom: 62 },
      tooltip: {
        trigger: "axis",
        backgroundColor: "rgba(8, 16, 29, 0.94)",
        borderColor: "#28415f",
        textStyle: { color: "#e6edf6" },
        valueFormatter: (value: unknown) => `${Number(value).toFixed(3)} ${unit}`,
      },
      xAxis: {
        type: "value",
        name: "time (s)",
        nameLocation: "middle",
        nameGap: 28,
        axisLine: { lineStyle: { color: "#3a4d66" } },
        axisLabel: { color: "#71849d" },
        splitLine: { lineStyle: { color: "#17263a" } },
      },
      yAxis: {
        type: "value",
        name: unit,
        axisLabel: { color: "#71849d" },
        splitLine: { lineStyle: { color: "#17263a" } },
      },
      dataZoom: [
        { type: "inside", filterMode: "none" },
        {
          type: "slider",
          height: 18,
          bottom: 10,
          borderColor: "#263b55",
          fillerColor: "rgba(33, 200, 181, 0.14)",
          handleStyle: { color: "#21c8b5" },
          textStyle: { color: "#71849d" },
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
  }, [group, series, title, unit]);

  return <div className="chart" ref={element} aria-label={`${title} chart`} />;
}
