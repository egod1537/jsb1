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
  Tab,
  Tabs,
} from "@blueprintjs/core";
import { useEffect, useMemo, useRef, useState } from "react";
import { ApiError, api } from "../../api/client";
import type { ControllerParameterDefinition, ScenarioCreateResponse, ScenarioValidationResult } from "../../types/api";
import "./scenarios.css";

interface RollHoldDraft {
  path: string;
  name: string;
  latitudeDeg: number;
  longitudeDeg: number;
  altitudeFt: number;
  airspeedKts: number;
  rollDeg: number;
  pitchDeg: number;
  headingDeg: number;
  pRadS: number;
  qRadS: number;
  rRadS: number;
  windEnabled: false;
  trimEnabled: boolean;
  trimMode: "Longitudinal" | "Full" | "Ground";
  durationSec: number;
  dtSec: number;
  commandTimeSec: number;
  commandRollDeg: number;
  controllerParameterIds: string[];
  settlingBandDeg: number;
  settlingTimeLimitSec: number;
  overshootLimitDeg: number;
  maxOscillationCycles: number;
}

type BuilderSectionId =
  | "general"
  | "initial_condition"
  | "environment"
  | "trim"
  | "simulation"
  | "events"
  | "controller_parameters"
  | "acceptance";

const BUILDER_SECTIONS: Array<{ id: BuilderSectionId; label: string }> = [
  { id: "general", label: "General" },
  { id: "initial_condition", label: "Initial Condition" },
  { id: "environment", label: "Environment" },
  { id: "trim", label: "Trim" },
  { id: "simulation", label: "Simulation" },
  { id: "events", label: "Events / Command" },
  { id: "controller_parameters", label: "Controller Parameters" },
  { id: "acceptance", label: "Acceptance" },
];

function sectionForValidationPath(path: string): BuilderSectionId {
  const root = path.split(/[.[\]]/, 1)[0];
  if (root === "initial_condition") return "initial_condition";
  if (root === "environment") return "environment";
  if (root === "trim") return "trim";
  if (root === "simulation") return "simulation";
  if (root === "events" || root === "command") return "events";
  if (root === "controller_parameters") return "controller_parameters";
  if (root === "acceptance") return "acceptance";
  return "general";
}

const ROLL_HOLD_TEST: RollHoldDraft = {
  path: "roll_hold_test.yaml",
  name: "Roll Hold Test",
  latitudeDeg: 0,
  longitudeDeg: 0,
  altitudeFt: 3000,
  airspeedKts: 100,
  rollDeg: 0,
  pitchDeg: 0,
  headingDeg: 0,
  pRadS: 0,
  qRadS: 0,
  rRadS: 0,
  windEnabled: false,
  trimEnabled: false,
  trimMode: "Full",
  durationSec: 30,
  dtSec: 0.01,
  commandTimeSec: 5,
  commandRollDeg: 5,
  controllerParameterIds: [],
  settlingBandDeg: 0.1,
  settlingTimeLimitSec: 20,
  overshootLimitDeg: 5,
  maxOscillationCycles: 10,
};

function yamlString(value: string): string {
  return JSON.stringify(value);
}

export function rollHoldDraftToYaml(draft: RollHoldDraft): string {
  return [
    "schema_version: 1",
    "scenario_type: roll_hold",
    `name: ${yamlString(draft.name)}`,
    "aircraft: c172x",
    ...(draft.controllerParameterIds.length > 0 ? [
      "controller_parameters:",
      ...draft.controllerParameterIds.map((id) => `  - ${id}`),
    ] : []),
    "",
    "initial_condition:",
    `  latitude_deg: ${draft.latitudeDeg}`,
    `  longitude_deg: ${draft.longitudeDeg}`,
    `  altitude_ft: ${draft.altitudeFt}`,
    `  airspeed_kts: ${draft.airspeedKts}`,
    `  roll_deg: ${draft.rollDeg}`,
    `  pitch_deg: ${draft.pitchDeg}`,
    `  heading_deg: ${draft.headingDeg}`,
    `  p_rad_s: ${draft.pRadS}`,
    `  q_rad_s: ${draft.qRadS}`,
    `  r_rad_s: ${draft.rRadS}`,
    "",
    "environment:",
    `  wind_enabled: ${draft.windEnabled}`,
    "",
    "trim:",
    `  enabled: ${draft.trimEnabled}`,
    `  mode: ${draft.trimMode}`,
    "",
    "simulation:",
    `  duration_sec: ${draft.durationSec}`,
    `  dt_sec: ${draft.dtSec}`,
    "",
    "events:",
    `  - time_sec: ${draft.commandTimeSec}`,
    "    command:",
    "      type: roll_hold",
    `      roll_deg: ${draft.commandRollDeg}`,
    "",
    "acceptance:",
    `  settling_band_deg: ${draft.settlingBandDeg}`,
    `  settling_time_limit_sec: ${draft.settlingTimeLimitSec}`,
    `  overshoot_limit_deg: ${draft.overshootLimitDeg}`,
    `  max_oscillation_cycles: ${draft.maxOscillationCycles}`,
    "",
  ].join("\n");
}

function slugForName(name: string): string {
  const slug = name
    .normalize("NFKD")
    .replace(/[\u0300-\u036f]/g, "")
    .toLowerCase()
    .replace(/[^a-z0-9]+/g, "_")
    .replace(/^_+|_+$/g, "");
  return `${slug || "roll_hold_scenario"}.yaml`;
}

function NumberField({
  label, value, onChange, min, max, step = 1,
}: {
  label: string;
  value: number;
  onChange: (value: number) => void;
  min?: number;
  max?: number;
  step?: number;
}) {
  return <FormGroup label={label}>
    <NumericInput
      aria-label={label}
      fill
      majorStepSize={step * 10}
      max={max}
      min={min}
      minorStepSize={step / 10}
      onValueChange={(number) => onChange(number)}
      stepSize={step}
      value={value}
    />
  </FormGroup>;
}

export function ScenarioBuilderDialog({
  isOpen,
  onClose,
  onSaved,
  onRunRequested,
}: {
  isOpen: boolean;
  onClose: () => void;
  onSaved: (result: ScenarioCreateResponse) => void | Promise<void>;
  onRunRequested?: (result: ScenarioCreateResponse) => void | Promise<void>;
}) {
  const [draft, setDraft] = useState<RollHoldDraft>(ROLL_HOLD_TEST);
  const [parameterDefinitions, setParameterDefinitions] = useState<ControllerParameterDefinition[]>([]);
  const [parametersLoading, setParametersLoading] = useState(false);
  const [parametersResolved, setParametersResolved] = useState(false);
  const [parametersError, setParametersError] = useState<string | null>(null);
  const [validation, setValidation] = useState<ScenarioValidationResult | null>(null);
  const [validating, setValidating] = useState(false);
  const [saving, setSaving] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [activeTab, setActiveTab] = useState<"form" | "yaml">("form");
  const [activeSection, setActiveSection] = useState<BuilderSectionId>("general");
  const wasOpen = useRef(false);

  useEffect(() => {
    if (isOpen && !wasOpen.current) {
      setDraft(ROLL_HOLD_TEST);
      setValidation(null);
      setError(null);
      setActiveTab("form");
      setActiveSection("general");
    }
    wasOpen.current = isOpen;
  }, [isOpen]);

  useEffect(() => {
    if (!isOpen) return;
    let active = true;
    setParametersLoading(true);
    setParametersResolved(false);
    setParametersError(null);
    api.runtimeParameters()
      .then((catalog) => {
        if (!active) return;
        const definitions = catalog.parameters ?? [];
        setParameterDefinitions(definitions);
        setDraft((current) => ({
          ...current,
          controllerParameterIds: current.controllerParameterIds.length > 0
            ? current.controllerParameterIds
            : definitions.map((item) => item.id),
        }));
      })
      .catch((reason: unknown) => {
        if (active) setParametersError(reason instanceof Error ? reason.message : "Could not load controller parameters");
      })
      .finally(() => {
        if (active) {
          setParametersLoading(false);
          setParametersResolved(true);
        }
      });
    return () => { active = false; };
  }, [isOpen]);

  const yaml = useMemo(() => rollHoldDraftToYaml(draft), [draft]);
  const update = <K extends keyof RollHoldDraft>(key: K, value: RollHoldDraft[K]) => {
    setDraft((current) => ({ ...current, [key]: value }));
    setValidation(null);
    setError(null);
  };
  const updateName = (name: string) => {
    setDraft((current) => ({
      ...current,
      name,
      path: current.path === slugForName(current.name) ? slugForName(name) : current.path,
    }));
    setValidation(null);
    setError(null);
  };

  const validate = async () => {
    setValidating(true);
    setError(null);
    try {
      const nextValidation = await api.validateScenario(yaml);
      setValidation(nextValidation);
      if (!nextValidation.valid && nextValidation.errors.length > 0) {
        setActiveTab("form");
        setActiveSection(sectionForValidationPath(nextValidation.errors[0].path));
      }
    } catch (reason) {
      setError(reason instanceof Error ? reason.message : "Could not validate scenario");
    } finally {
      setValidating(false);
    }
  };

  const save = async (runAfterSave = false) => {
    if (!validation?.valid) return;
    setSaving(true);
    setError(null);
    try {
      const result = await api.createScenario(draft.path, yaml);
      if (runAfterSave && onRunRequested) {
        await onRunRequested(result);
      } else {
        await onSaved(result);
      }
      onClose();
    } catch (reason) {
      if (reason instanceof ApiError && reason.detail && typeof reason.detail === "object") {
        const detail = reason.detail as { validation?: ScenarioValidationResult };
        if (detail.validation) setValidation(detail.validation);
      }
      setError(reason instanceof Error ? reason.message : "Could not save scenario");
    } finally {
      setSaving(false);
    }
  };

  const validationErrorsBySection = useMemo(() => {
    const counts = new Map<BuilderSectionId, number>();
    for (const item of validation?.errors ?? []) {
      const section = sectionForValidationPath(item.path);
      counts.set(section, (counts.get(section) ?? 0) + 1);
    }
    return counts;
  }, [validation]);
  const parameterValidationErrors = useMemo(() => {
    if (!parametersResolved || parametersError) return [];
    const supported = new Set(parameterDefinitions.map((item) => item.id));
    return draft.controllerParameterIds
      .filter((id) => !supported.has(id))
      .map((id) => `Unsupported controller parameter for the configured JSB0 contract: ${id}`);
  }, [draft.controllerParameterIds, parameterDefinitions, parametersError, parametersResolved]);

  const sectionPanel: Record<BuilderSectionId, React.ReactNode> = {
    general: <section className="scenario-builder-section" aria-labelledby="scenario-builder-section-general">
      <h3 id="scenario-builder-section-general">General</h3>
      <div className="scenario-builder-fields">
        <FormGroup label="Start from"><HTMLSelect aria-label="Scenario preset" fill onChange={() => undefined} value="roll-hold-test" options={[{ label: "Roll Hold Test", value: "roll-hold-test" }]} /></FormGroup>
        <FormGroup label="Name"><InputGroup aria-label="Name" value={draft.name} onChange={(event) => updateName(event.currentTarget.value)} /></FormGroup>
        <FormGroup label="File path" helperText="Relative .yaml/.yml path in the managed scenario library."><InputGroup aria-label="File path" value={draft.path} onChange={(event) => update("path", event.currentTarget.value)} /></FormGroup>
        <FormGroup label="Scenario type"><InputGroup disabled value="roll_hold" /></FormGroup>
        <FormGroup label="Aircraft"><InputGroup disabled value="c172x" /></FormGroup>
        <Callout compact className="scenario-builder-separation-note">Scenario fields define experiment conditions. Controller parameters are frozen separately at Run creation.</Callout>
      </div>
    </section>,
    initial_condition: <section className="scenario-builder-section" aria-labelledby="scenario-builder-section-initial">
      <h3 id="scenario-builder-section-initial">Initial Condition</h3>
      <div className="scenario-builder-fields scenario-builder-fields-dense">
        <NumberField label="Latitude [deg]" min={-90} max={90} step={0.1} value={draft.latitudeDeg} onChange={(value) => update("latitudeDeg", value)} />
        <NumberField label="Longitude [deg]" min={-180} max={180} step={0.1} value={draft.longitudeDeg} onChange={(value) => update("longitudeDeg", value)} />
        <NumberField label="Altitude [ft]" value={draft.altitudeFt} onChange={(value) => update("altitudeFt", value)} />
        <NumberField label="Airspeed [kt]" min={0} value={draft.airspeedKts} onChange={(value) => update("airspeedKts", value)} />
        <NumberField label="Roll [deg]" step={0.1} value={draft.rollDeg} onChange={(value) => update("rollDeg", value)} />
        <NumberField label="Pitch [deg]" step={0.1} value={draft.pitchDeg} onChange={(value) => update("pitchDeg", value)} />
        <NumberField label="Heading [deg]" step={0.1} value={draft.headingDeg} onChange={(value) => update("headingDeg", value)} />
        <NumberField label="p [rad/s]" step={0.01} value={draft.pRadS} onChange={(value) => update("pRadS", value)} />
        <NumberField label="q [rad/s]" step={0.01} value={draft.qRadS} onChange={(value) => update("qRadS", value)} />
        <NumberField label="r [rad/s]" step={0.01} value={draft.rRadS} onChange={(value) => update("rRadS", value)} />
      </div>
    </section>,
    environment: <section className="scenario-builder-section" aria-labelledby="scenario-builder-section-environment">
      <h3 id="scenario-builder-section-environment">Environment</h3>
      <Checkbox checked={draft.windEnabled} disabled label="Wind enabled (the selected contract currently requires disabled)" />
    </section>,
    trim: <section className="scenario-builder-section" aria-labelledby="scenario-builder-section-trim">
      <h3 id="scenario-builder-section-trim">Trim</h3>
      <div className="scenario-builder-fields">
        <Checkbox checked={draft.trimEnabled} label="Enabled" onChange={(event) => update("trimEnabled", event.currentTarget.checked)} />
        <FormGroup label="Mode"><HTMLSelect aria-label="Trim mode" fill value={draft.trimMode} onChange={(event) => update("trimMode", event.currentTarget.value as RollHoldDraft["trimMode"])} options={["Longitudinal", "Full", "Ground"]} /></FormGroup>
      </div>
    </section>,
    simulation: <section className="scenario-builder-section" aria-labelledby="scenario-builder-section-simulation">
      <h3 id="scenario-builder-section-simulation">Simulation</h3>
      <div className="scenario-builder-fields">
        <NumberField label="Duration [s]" min={0.000001} step={1} value={draft.durationSec} onChange={(value) => update("durationSec", value)} />
        <NumberField label="Time step [s]" min={0.000001} step={0.001} value={draft.dtSec} onChange={(value) => update("dtSec", value)} />
      </div>
    </section>,
    events: <section className="scenario-builder-section" aria-labelledby="scenario-builder-section-events">
      <h3 id="scenario-builder-section-events">Events / Command</h3>
      <div className="scenario-builder-fields">
        <NumberField label="Command time [s]" min={0} step={0.1} value={draft.commandTimeSec} onChange={(value) => update("commandTimeSec", value)} />
        <NumberField label="Roll command [deg]" step={0.1} value={draft.commandRollDeg} onChange={(value) => update("commandRollDeg", value)} />
      </div>
    </section>,
    controller_parameters: <section className="scenario-builder-section" aria-labelledby="scenario-builder-section-controller-parameters">
      <h3 id="scenario-builder-section-controller-parameters">Controller Parameters</h3>
      {parametersError ? <Callout compact intent={Intent.DANGER}>{parametersError}</Callout>
        : parametersLoading ? <Callout compact>Loading controller parameter metadata…</Callout>
          : <div className="scenario-builder-parameter-whitelist">
            {parameterDefinitions.map((item) => <Checkbox
              key={item.id}
              checked={draft.controllerParameterIds.includes(item.id)}
              onChange={(event) => update("controllerParameterIds", event.currentTarget.checked
                ? [...draft.controllerParameterIds, item.id]
                : draft.controllerParameterIds.filter((id) => id !== item.id))}
            ><span><strong>{item.display_name}</strong><code>{item.id}</code><small>{item.description}</small></span></Checkbox>)}
          </div>}
      <Callout compact className="scenario-builder-parameter-note">The Scenario stores only this tunable parameter whitelist. Runtime metadata and Run values remain separate.</Callout>
    </section>,
    acceptance: <section className="scenario-builder-section" aria-labelledby="scenario-builder-section-acceptance">
      <h3 id="scenario-builder-section-acceptance">Acceptance</h3>
      <div className="scenario-builder-fields scenario-builder-fields-dense">
        <NumberField label="Settling band [deg]" min={0} step={0.1} value={draft.settlingBandDeg} onChange={(value) => update("settlingBandDeg", value)} />
        <NumberField label="Settling time limit [s]" min={0} step={0.5} value={draft.settlingTimeLimitSec} onChange={(value) => update("settlingTimeLimitSec", value)} />
        <NumberField label="Overshoot limit [deg]" min={0} step={0.1} value={draft.overshootLimitDeg} onChange={(value) => update("overshootLimitDeg", value)} />
        <NumberField label="Max oscillation cycles" min={0} step={1} value={draft.maxOscillationCycles} onChange={(value) => update("maxOscillationCycles", value)} />
      </div>
    </section>,
  };

  const form = <div className="scenario-builder-form-layout">
    <nav className="scenario-builder-section-nav" aria-label="Scenario sections">
      {BUILDER_SECTIONS.map((section) => {
        const errorCount = validationErrorsBySection.get(section.id) ?? 0;
        return <button
          key={section.id}
          type="button"
          aria-current={activeSection === section.id ? "step" : undefined}
          className={activeSection === section.id ? "is-active" : undefined}
          onClick={() => setActiveSection(section.id)}
        >
          <span>{section.label}</span>
          {errorCount > 0
            ? <span className="scenario-builder-section-error" aria-label={`${errorCount} validation ${errorCount === 1 ? "error" : "errors"}`}>{errorCount}</span>
            : section.id === "controller_parameters" && parameterValidationErrors.length > 0
              ? <span className="scenario-builder-section-error" aria-label={`${parameterValidationErrors.length} parameter validation errors`}>{parameterValidationErrors.length}</span>
            : validation?.valid && <span className="scenario-builder-section-valid" aria-label="Valid">✓</span>}
        </button>;
      })}
    </nav>
    <div className="scenario-builder-main" data-section={activeSection}>
      {sectionPanel[activeSection]}
    </div>
  </div>;

  return <Dialog className="scenario-builder-dialog" isOpen={isOpen} onClose={onClose} title="New Scenario">
    <DialogBody className="scenario-builder-dialog-body">
      <Tabs
        animate={false}
        className="scenario-builder-tabs"
        id="scenario-builder-tabs"
        onChange={(tabId) => setActiveTab(tabId as "form" | "yaml")}
        renderActiveTabPanelOnly
        selectedTabId={activeTab}
      >
        <Tab id="form" title="Form" panel={form} />
        <Tab id="yaml" title="Raw YAML" panel={<pre className="scenario-raw-yaml" aria-label="Scenario YAML preview">{yaml}</pre>} />
      </Tabs>
      <div className="scenario-builder-validation" aria-live="polite">
        {validation == null && <Callout compact intent={Intent.NONE}>Modified · Needs validation</Callout>}
        {validation?.valid && <Callout compact intent={Intent.SUCCESS}>
          Valid against JSB0 {validation.runtime?.branch ?? "configured revision"} @ {validation.runtime?.commit.slice(0, 12)}
        </Callout>}
        {validation && !validation.valid && <Callout compact intent={Intent.DANGER} title="Scenario is invalid">
          <ul>{validation.errors.map((item, index) => <li key={`${item.path}-${index}`}><code>{item.path}</code>: {item.message}</li>)}</ul>
        </Callout>}
        {error && <Callout compact intent={Intent.DANGER}>{error}</Callout>}
      </div>
    </DialogBody>
    <DialogFooter actions={<>
      <Button disabled={saving} onClick={onClose}>Cancel</Button>
      <Button icon="confirm" loading={validating} onClick={validate}>Validate</Button>
      <Button disabled={!validation?.valid || validating} loading={saving} onClick={() => save(false)}>Save Scenario</Button>
      {onRunRequested && <Button
        disabled={
          !validation?.valid
          || validating
          || parametersLoading
          || !parametersResolved
          || parametersError != null
          || parameterValidationErrors.length > 0
        }
        intent={Intent.PRIMARY}
        loading={saving}
        onClick={() => save(true)}
      >Save &amp; Run</Button>}
    </>} />
  </Dialog>;
}
