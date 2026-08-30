import { Callout, HTMLTable, Intent, Tab, Tabs, Tag } from "@blueprintjs/core";
import type { ReactNode } from "react";
import type { ScenarioInspectionDetail, ScenarioInspectionValidation } from "../../types/api";
import "./scenarios.css";

const labels: Record<string, string> = {
  aircraft: "Aircraft",
  altitude_ft: "Altitude",
  airspeed_kts: "Airspeed",
  roll_deg: "Roll",
  pitch_deg: "Pitch",
  heading_deg: "Heading",
  wind_enabled: "Wind",
  enabled: "Enabled",
  mode: "Mode",
  duration_sec: "Duration",
  time_step_sec: "Time step",
  dt_sec: "Time step",
  start_sec: "Start",
  settling_band_deg: "Settling band",
  settling_time_limit_sec: "Settling time limit",
  overshoot_limit_deg: "Overshoot limit",
  max_oscillation_cycles: "Oscillation cycles",
};

const units: Record<string, string> = {
  altitude_ft: "ft",
  airspeed_kts: "kt",
  roll_deg: "deg",
  pitch_deg: "deg",
  heading_deg: "deg",
  duration_sec: "s",
  time_step_sec: "s",
  dt_sec: "s",
  start_sec: "s",
  settling_band_deg: "deg",
  settling_time_limit_sec: "s",
  overshoot_limit_deg: "deg",
};

function titleFor(key: string) {
  return labels[key] ?? key.replaceAll("_", " ").replace(/^./, (value) => value.toUpperCase());
}

function displayValue(key: string, value: unknown): ReactNode {
  if (typeof value === "boolean") return value ? "Enabled" : "Disabled";
  if (value == null) return "—";
  if (key === "settling_band_deg" && typeof value === "number") {
    return <span className="technical-value">±{value} deg</span>;
  }
  if (typeof value === "number") return <span className="technical-value">{value}{units[key] ? ` ${units[key]}` : ""}</span>;
  if (typeof value === "string") return value;
  return <code>{JSON.stringify(value)}</code>;
}

function PropertyRows({ value }: { value: Record<string, unknown> }) {
  return <dl className="scenario-property-list">
    {Object.entries(value).map(([key, item]) => <div key={key}>
      <dt>{titleFor(key)}</dt>
      <dd>{displayValue(key, item)}</dd>
    </div>)}
  </dl>;
}

function GenericTree({ value, depth = 0 }: { value: unknown; depth?: number }) {
  if (Array.isArray(value)) return <ol className="scenario-tree-list">
    {value.map((item, index) => <li key={index}><GenericTree value={item} depth={depth + 1} /></li>)}
  </ol>;
  if (value && typeof value === "object") return <div className="scenario-tree">
    {Object.entries(value as Record<string, unknown>).map(([key, item]) => <div className="scenario-tree-row" key={key}>
      <span>{titleFor(key)}</span>
      <div>{item && typeof item === "object" ? <GenericTree value={item} depth={depth + 1} /> : displayValue(key, item)}</div>
    </div>)}
  </div>;
  return <>{String(value ?? "—")}</>;
}

function Section({ title, value }: { title: string; value: unknown }) {
  if (value == null) return null;
  return <section className="scenario-definition-section">
    <header>{title}</header>
    {value && typeof value === "object" && !Array.isArray(value)
      ? <PropertyRows value={value as Record<string, unknown>} />
      : <GenericTree value={value} />}
  </section>;
}

function Definition({ scenario }: { scenario: ScenarioInspectionDetail }) {
  const definition = scenario.definition;
  if (!definition) return <Callout compact intent={Intent.WARNING}>The definition could not be parsed. Inspect Raw YAML and Validation.</Callout>;
  if (scenario.scenario_type !== "roll_hold") return <GenericTree value={definition} />;
  const consumed = new Set([
    "schema_version", "scenario_type", "name", "aircraft", "initial_condition",
    "environment", "trim", "simulation", "command", "acceptance",
  ]);
  const remainder = Object.fromEntries(Object.entries(definition).filter(([key]) => !consumed.has(key)));
  return <div className="scenario-definition-grid">
    <Section title="Aircraft" value={{ aircraft: definition.aircraft }} />
    <Section title="Initial Conditions" value={definition.initial_condition} />
    <Section title="Environment" value={definition.environment} />
    <Section title="Trim" value={definition.trim} />
    <Section title="Simulation" value={definition.simulation} />
    <Section title="Command / Events" value={definition.command} />
    <Section title="Acceptance" value={definition.acceptance} />
    {Object.keys(remainder).length > 0 && <Section title="Additional Definition" value={remainder} />}
  </div>;
}

export function ScenarioValidationTag({ validation }: { validation: ScenarioInspectionValidation }) {
  if (validation.valid === true) return <Tag minimal intent={Intent.SUCCESS}>VALID</Tag>;
  if (validation.valid === false) return <Tag minimal intent={Intent.DANGER}>INVALID</Tag>;
  return <Tag minimal>UNKNOWN</Tag>;
}

function Overview({ scenario }: { scenario: ScenarioInspectionDetail }) {
  const runtime = [scenario.validation.runtime_branch, scenario.validation.runtime_commit?.slice(0, 12)]
    .filter(Boolean).join(" @ ");
  return <>
    {scenario.provenance.integrity === "mismatch" && <Callout compact intent={Intent.DANGER} title="Snapshot checksum mismatch">
      The frozen scenario bytes no longer match the checksum stored with this execution.
    </Callout>}
    <HTMLTable compact className="scenario-overview-table">
      <tbody>
        <tr><th>Name</th><td>{scenario.name}</td></tr>
        <tr><th>Type</th><td><code>{scenario.scenario_type ?? "—"}</code></td></tr>
        <tr><th>Schema</th><td>{scenario.schema_version == null ? "—" : `v${scenario.schema_version}`}</td></tr>
        <tr><th>Source</th><td><Tag minimal>{scenario.source.toUpperCase()}</Tag></td></tr>
        <tr><th>Path</th><td><code>{scenario.path}</code></td></tr>
        <tr><th>SHA256</th><td><code title={scenario.sha256 ?? undefined}>{scenario.sha256 ?? "—"}</code></td></tr>
        <tr><th>Validation</th><td><ScenarioValidationTag validation={scenario.validation} /></td></tr>
        <tr><th>{scenario.source === "run_snapshot" ? "Executed with" : "Validated against"}</th><td><code>{runtime || "Unavailable"}</code></td></tr>
        <tr><th>{scenario.source === "sftp" ? "Cached / synced" : "Updated"}</th><td className="technical-value">{scenario.updated_at ? new Date(scenario.updated_at).toLocaleString() : "—"}</td></tr>
        <tr><th>Authority</th><td>{scenario.provenance.authority}</td></tr>
        <tr><th>Integrity</th><td><code>{scenario.provenance.integrity}</code></td></tr>
      </tbody>
    </HTMLTable>
  </>;
}

function YamlText({ value }: { value: string }) {
  const lines = value.split("\n");
  return <pre className="scenario-raw-yaml"><code>{lines.map((line, index) => {
    const comment = line.match(/^(\s*)(#.*)$/);
    const property = line.match(/^(\s*)([^\s#][^:]*:)(.*)$/);
    return <span className="scenario-yaml-line" key={index}>
      {comment ? <>{comment[1]}<span className="yaml-comment">{comment[2]}</span></>
        : property ? <>{property[1]}<span className="yaml-key">{property[2]}</span><span className="yaml-value">{property[3]}</span></>
        : line}
      {index < lines.length - 1 ? "\n" : ""}
    </span>;
  })}</code></pre>;
}

function Validation({ scenario }: { scenario: ScenarioInspectionDetail }) {
  return <div className="scenario-validation-view">
    <div className="scenario-validation-heading">
      <ScenarioValidationTag validation={scenario.validation} />
      <span>{scenario.validation.valid === true ? "Compatible with the selected JSB0 contract" : scenario.validation.valid === false ? "Scenario contract validation failed" : "Historical contract unavailable"}</span>
    </div>
    <PropertyRows value={{
      runtime_branch: scenario.validation.runtime_branch,
      runtime_commit: scenario.validation.runtime_commit,
      schema_version: scenario.schema_version,
      integrity: scenario.provenance.integrity,
    }} />
    {scenario.validation.errors.length > 0 && <ol className="scenario-validation-errors">
      {scenario.validation.errors.map((error, index) => <li key={`${error.path}-${error.code}-${index}`}>
        <code>{error.path}</code>
        <span>{error.message}</span>
        <small>{error.code}</small>
      </li>)}
    </ol>}
  </div>;
}

export function ScenarioViewer({ scenario }: { scenario: ScenarioInspectionDetail }) {
  return <div className="scenario-viewer">
    <Tabs animate={false} id={`scenario-${scenario.id}`} renderActiveTabPanelOnly>
      <Tab id="overview" title="Overview" panel={<Overview scenario={scenario} />} />
      <Tab id="definition" title="Definition" panel={<Definition scenario={scenario} />} />
      <Tab id="raw" title="Raw YAML" panel={scenario.raw_yaml == null
        ? <Callout compact intent={Intent.WARNING}>Raw YAML is unavailable for this rejected remote revision.</Callout>
        : <YamlText value={scenario.raw_yaml} />} />
      <Tab id="validation" title="Validation" panel={<Validation scenario={scenario} />} />
    </Tabs>
  </div>;
}
