import { Button, ButtonGroup, Callout, Menu, MenuDivider, MenuItem, Spinner, Tag, Tooltip } from "@blueprintjs/core";
import { Select } from "@blueprintjs/select";
import { Fragment, useEffect, useId, useMemo, useState } from "react";
import type { SignalMetadata } from "../../types/api";
import { PlotGrid } from "./PlotGrid";
import { PlotSettingsDialog } from "./PlotSettingsDialog";
import { MaximizedPlotDialog } from "./MaximizedPlotDialog";
import {
  ANALYSIS_PRESETS,
  ANALYSIS_PRESET_REGISTRY,
  CUSTOM_PRESET,
  defaultPresetForScenario,
  presetAvailability,
} from "./presets";
import {
  PLOT_LAYOUTS,
  arrangePlots,
  signalUnion,
  type PlotDataSource,
  type HorizontalReferenceLine,
  type PlotConfig,
  type PlotLayoutId,
  type PlotPreset,
  type PlotSettingsValue,
  type TimelineState,
  type WorkspaceTelemetry,
} from "./plotTypes";
import type { PlotWorkspaceController } from "./workspaceState";
import "./plots.css";

interface Props {
  dataSource: PlotDataSource;
  scenarioType?: string | null;
  comparisonView?: "overlay" | "side-by-side";
  onComparisonViewChange?: (view: "overlay" | "side-by-side") => void;
  workspace: PlotWorkspaceController;
  displayMode?: "normal" | "dialog";
  onMaximizeWorkspace?: () => void;
  timeline: TimelineState;
  onVisibleRangeChange: (start: number, end: number) => void;
  onCursorTimeChange: (time: number | null) => void;
  onFullTimeRangeChange: (start: number, end: number) => void;
  onResetView: () => void;
  acceptanceBandDeg?: number;
  horizontalReferenceLinesByPlot?: Record<string, HorizontalReferenceLine[]>;
}

type DiscoveryStatus = "loading" | "ready" | "unavailable";
type PlotSettingsTarget =
  | { mode: "create"; slot: number }
  | { mode: "edit"; plotId: string };

function timeExtent(telemetry: WorkspaceTelemetry | null): [number, number] {
  if (!telemetry || telemetry.time.length === 0) return [0, 0];
  return [telemetry.time[0], telemetry.time[telemetry.time.length - 1]];
}

function emptyTelemetry(units: Record<string, string> = {}): WorkspaceTelemetry {
  return { time: [], series: {}, units, source_points: 0, returned_points: 0 };
}

export function AnalysisWorkspace({
  dataSource,
  scenarioType,
  comparisonView = "overlay",
  onComparisonViewChange,
  workspace,
  displayMode = "normal",
  onMaximizeWorkspace,
  timeline,
  onVisibleRangeChange,
  onCursorTimeChange,
  onFullTimeRangeChange,
  onResetView,
  acceptanceBandDeg,
  horizontalReferenceLinesByPlot,
}: Props) {
  const recommendedPreset = defaultPresetForScenario(scenarioType);
  const { layout, activePresetId, presetModified, plots, layoutItems } = workspace;
  const [plotSettingsTarget, setPlotSettingsTarget] = useState<PlotSettingsTarget | null>(null);
  const [expandedPlotId, setExpandedPlotId] = useState<string | null>(null);
  const [telemetry, setTelemetry] = useState<WorkspaceTelemetry | null>(null);
  const [availableSignals, setAvailableSignals] = useState<SignalMetadata[]>([]);
  const [discoveryStatus, setDiscoveryStatus] = useState<DiscoveryStatus>("loading");
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);
  const workspaceInstanceId = useId();
  const requestedSignals = useMemo(() => signalUnion(plots), [plots]);
  const signalKey = requestedSignals.join(",");
  const availableSignalKey = availableSignals.map((signal) => signal.name).sort().join(",");
  const activePreset = ANALYSIS_PRESET_REGISTRY.get(activePresetId) ?? CUSTOM_PRESET;
  const arrangedPlots = useMemo(
    () => arrangePlots(plots, layoutItems, layout),
    [layout, layoutItems, plots],
  );
  const presentationPlots = useMemo(() => {
    if (comparisonView !== "side-by-side" || !dataSource.variants?.length) return arrangedPlots;
    return arrangedPlots.flatMap((plot) => dataSource.variants!.map((variant, index) => ({
      ...plot,
      id: `${plot.id}::${variant}`,
      sourcePlotId: plot.id,
      presentationVariant: variant,
      title: `${variant} · ${plot.title}`,
      slot: plot.slot * dataSource.variants!.length + index,
    })));
  }, [arrangedPlots, comparisonView, dataSource.variants]);
  const sourcePlotId = (id: string) => presentationPlots.find((plot) => plot.id === id)?.sourcePlotId ?? id;

  useEffect(() => {
    let active = true;
    setTelemetry(null);
    setAvailableSignals([]);
    setError(null);
    if (!dataSource.listSignals) {
      setDiscoveryStatus("unavailable");
      return () => { active = false; };
    }
    setDiscoveryStatus("loading");
    dataSource.listSignals()
      .then((signals) => {
        if (!active) return;
        setAvailableSignals([...signals].sort((left, right) => left.name.localeCompare(right.name)));
        setDiscoveryStatus("ready");
      })
      .catch(() => {
        if (!active) return;
        // Older backends can still use the batched signal request and its
        // missing-channel retry without blocking analysis.
        setDiscoveryStatus("unavailable");
      });
    return () => { active = false; };
  }, [dataSource, dataSource.key]);

  useEffect(() => {
    if (discoveryStatus === "loading") return;
    const available = new Set(availableSignals.map((signal) => signal.name));
    const loadableSignals = discoveryStatus === "ready"
      ? requestedSignals.filter((signal) => available.has(signal))
      : requestedSignals;
    if (loadableSignals.length === 0) {
      const units = Object.fromEntries(availableSignals.map((signal) => [signal.name, signal.unit]));
      setTelemetry(emptyTelemetry(units));
      setLoading(false);
      setError(null);
      return;
    }
    let active = true;
    setLoading(true);
    setError(null);
    dataSource.loadSignals(loadableSignals)
      .then((response) => {
        if (!active) return;
        setTelemetry(response);
        if (discoveryStatus === "unavailable") {
          setAvailableSignals((current) => {
            const metadata = new Map(current.map((signal) => [signal.name, signal]));
            for (const name of Object.keys(response.series)) metadata.set(name, { name, unit: response.units[name] ?? "raw" });
            return [...metadata.values()].sort((left, right) => left.name.localeCompare(right.name));
          });
        }
        const [start, end] = timeExtent(response);
        onFullTimeRangeChange(start, end);
        setLoading(false);
      })
      .catch((reason: Error) => {
        if (!active) return;
        setError(reason.message);
        setLoading(false);
      });
    return () => { active = false; };
  // Timeline changes never trigger telemetry fetches.
  // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [dataSource, dataSource.key, signalKey, discoveryStatus, availableSignalKey, onFullTimeRangeChange]);

  const applyPreset = (preset: PlotPreset) => {
    workspace.applyPreset(preset);
    setExpandedPlotId(null);
  };
  const openCreatePlot = (slot?: number) => {
    const capacity = PLOT_LAYOUTS[layout].capacity;
    const occupied = new Set(arrangedPlots.map((plot) => plot.slot));
    const targetSlot = slot ?? Array.from({ length: capacity }, (_, index) => index).find((index) => !occupied.has(index));
    if (targetSlot == null || targetSlot >= capacity || occupied.has(targetSlot)) return;
    setPlotSettingsTarget({ mode: "create", slot: targetSlot });
  };
  const openEditPlot = (id: string) => {
    const plotId = sourcePlotId(id);
    if (plots.some((plot) => plot.id === plotId)) setPlotSettingsTarget({ mode: "edit", plotId });
  };
  const applyPlotSettings = (settings: PlotSettingsValue) => {
    if (!plotSettingsTarget) return;
    if (plotSettingsTarget.mode === "create") {
      workspace.addPlot(plotSettingsTarget.slot, settings);
    } else {
      workspace.updatePlot(plotSettingsTarget.plotId, settings);
    }
    setPlotSettingsTarget(null);
  };
  const closePlot = (id: string) => {
    workspace.removePlot(id);
    setExpandedPlotId((current) => current === id ? null : current);
  };
  const capacity = PLOT_LAYOUTS[layout].capacity;
  const hasEmptySlot = Array.from({ length: capacity }, (_, slot) => slot).some((slot) => !arrangedPlots.some((plot) => plot.slot === slot));
  const settingsPlot: PlotConfig | undefined = plotSettingsTarget?.mode === "edit"
    ? plots.find((plot) => plot.id === plotSettingsTarget.plotId)
    : undefined;
  const expandedPlot = expandedPlotId == null
    ? null
    : presentationPlots.find((plot) => plot.id === expandedPlotId) ?? null;
  const presetAvailabilityLabel = (preset: PlotPreset) => {
    const availability = presetAvailability(preset, availableSignals.map((signal) => signal.name));
    return availability.total === 0 ? undefined : `${availability.available}/${availability.total}`;
  };
  const presetDisabled = (preset: PlotPreset) => {
    const availability = presetAvailability(preset, availableSignals.map((signal) => signal.name));
    return discoveryStatus === "ready" && !availability.usable;
  };
  const recommendedOptions = ANALYSIS_PRESETS.filter((preset) => preset.id === recommendedPreset.id);
  const generalOptions = ANALYSIS_PRESETS.filter((preset) => preset.category !== "control" && preset.id !== recommendedPreset.id);
  const otherOptions = ANALYSIS_PRESETS.filter((preset) => preset.category === "control" && preset.id !== recommendedPreset.id);
  const presetGroups = [
    { label: "Recommended", presets: recommendedOptions },
    { label: "General", presets: generalOptions },
    { label: "Other", presets: otherOptions },
  ].filter((group) => group.presets.length > 0);

  return <section className={`analysis-workspace${displayMode === "dialog" ? " analysis-workspace-dialog" : ""}`} aria-label={displayMode === "dialog" ? "Maximized plot workspace" : "Plot workspace"}>
    <header className="workspace-toolbar">
      <div className="workspace-toolbar-group">
        <span>Preset</span>
        <Select<PlotPreset>
          items={ANALYSIS_PRESETS}
          itemsEqual="id"
          itemDisabled={presetDisabled}
          filterable={false}
          onItemSelect={applyPreset}
          itemRenderer={(preset, { handleClick, handleFocus, modifiers, ref }) => !modifiers.matchesPredicate ? null : <MenuItem
            key={preset.id}
            ref={ref}
            id={`plot-preset-${workspaceInstanceId}-${preset.id}`}
            roleStructure="listoption"
            active={modifiers.active}
            selected={preset.id === activePresetId}
            disabled={modifiers.disabled}
            className={preset.id === activePresetId ? "plot-preset-menu-item-selected" : undefined}
            text={preset.name}
            label={presetAvailabilityLabel(preset)}
            onClick={handleClick}
            onFocus={handleFocus}
          />}
          itemListRenderer={({ items, itemsParentRef, menuProps, renderItem }) => <Menu
            {...menuProps}
            ulRef={itemsParentRef}
            className="plot-preset-menu"
            aria-label="Plot preset options"
            role="listbox"
          >
            {presetGroups.map((group) => <Fragment key={group.label}>
              <MenuDivider title={group.label} />
              {group.presets.map((preset) => renderItem(preset, items.indexOf(preset)))}
            </Fragment>)}
          </Menu>}
          popoverProps={{ minimal: true, placement: "bottom-start" }}
          popoverContentProps={{ className: "plot-preset-popover" }}
          popoverTargetProps={{ className: "plot-preset-select-target", "aria-label": "Plot preset" }}
        >
          <Tooltip content={activePreset.description ?? activePreset.name}>
            <Button
              className="plot-preset-trigger"
              text={activePreset.name}
              rightIcon="caret-down"
              minimal
              small
            />
          </Tooltip>
        </Select>
        {presetModified && <Tag minimal>Modified</Tag>}
        <Button small minimal disabled={!presetModified} onClick={() => applyPreset(activePreset)}>Reset Preset</Button>
      </div>
      {dataSource.variants && dataSource.variants.length > 1 && <div className="workspace-toolbar-group">
        <span>View</span>
        <ButtonGroup minimal>
          <Button small active={comparisonView === "overlay"} onClick={() => onComparisonViewChange?.("overlay")}>Overlay</Button>
          <Button small active={comparisonView === "side-by-side"} onClick={() => onComparisonViewChange?.("side-by-side")}>Side-by-side</Button>
        </ButtonGroup>
      </div>}
      <div className="workspace-toolbar-group">
        <span>Layout</span>
        <ButtonGroup minimal>
          {(Object.keys(PLOT_LAYOUTS) as PlotLayoutId[]).map((id) => <Button
            key={id}
            aria-label={`Use ${id} plot layout`}
            active={layout === id}
            small
            onClick={() => workspace.setLayout(id)}
          >{id}</Button>)}
        </ButtonGroup>
      </div>
      <div className="workspace-toolbar-actions">
        <Button icon="add" small disabled={!hasEmptySlot} onClick={() => openCreatePlot()}>Plot</Button>
        <Button icon="reset" small onClick={onResetView}>Reset View</Button>
        {onMaximizeWorkspace && <Tooltip content="Maximize workspace">
          <Button aria-label="Maximize workspace" icon="fullscreen" minimal small onClick={onMaximizeWorkspace} />
        </Tooltip>}
      </div>
      <div className="workspace-readout" aria-label="Timeline readout">
        {timeline.cursorTime == null
          ? `${timeline.visibleStart.toFixed(2)}–${timeline.visibleEnd.toFixed(2)} s`
          : `t = ${timeline.cursorTime.toFixed(3)} s`}
        {telemetry && <Tooltip content={`${telemetry.returned_points.toLocaleString()} of ${telemetry.source_points.toLocaleString()} source points`}>
          <span>{telemetry.returned_points.toLocaleString()} pts</span>
        </Tooltip>}
      </div>
    </header>
    <div className="plot-area" data-layout={layout}>
      {loading && <div className="workspace-notice"><Spinner size={14} /><span>Loading telemetry</span></div>}
      {error && <Callout intent="danger" compact title="Telemetry unavailable">{error}</Callout>}
      <PlotGrid
        layout={layout}
        plots={presentationPlots}
        telemetry={telemetry}
        availableSignals={availableSignals}
        timeline={timeline}
        onAddPlot={openCreatePlot}
        onConfigurePlot={openEditPlot}
        onVisibleRangeChange={onVisibleRangeChange}
        onCursorTimeChange={onCursorTimeChange}
        onMaximize={setExpandedPlotId}
        onClose={(id) => { setExpandedPlotId(null); closePlot(sourcePlotId(id)); }}
        onMovePlot={(id, targetSlot) => {
          const plotId = sourcePlotId(id);
          const sourceTargetSlot = comparisonView === "side-by-side" && dataSource.variants?.length
            ? Math.floor(targetSlot / dataSource.variants.length)
            : targetSlot;
          workspace.movePlot(plotId, sourceTargetSlot);
        }}
        resolveSignal={dataSource.resolveSignal}
        hasSignal={dataSource.hasSignal}
        acceptanceBandDeg={acceptanceBandDeg}
        horizontalReferenceLinesByPlot={horizontalReferenceLinesByPlot}
      />
    </div>
    <MaximizedPlotDialog
      plot={expandedPlot}
      telemetry={telemetry}
      availableSignals={availableSignals}
      timeline={timeline}
      onVisibleRangeChange={onVisibleRangeChange}
      onCursorTimeChange={onCursorTimeChange}
      onClose={() => setExpandedPlotId(null)}
      resolveSignal={dataSource.resolveSignal}
      hasSignal={dataSource.hasSignal}
      acceptanceBandDeg={acceptanceBandDeg}
      horizontalReferenceLines={expandedPlot ? horizontalReferenceLinesByPlot?.[sourcePlotId(expandedPlot.id)] : undefined}
    />
    <PlotSettingsDialog
      mode={plotSettingsTarget?.mode ?? "create"}
      isOpen={plotSettingsTarget != null}
      initialPlot={settingsPlot}
      availableSignals={availableSignals}
      onCancel={() => setPlotSettingsTarget(null)}
      onApply={applyPlotSettings}
    />
  </section>;
}
