import {
  Button,
  Callout,
  Dialog,
  DialogBody,
  DialogFooter,
  FormGroup,
  HTMLSelect,
  Intent,
  Spinner,
} from "@blueprintjs/core";
import { IconNames } from "@blueprintjs/icons";
import { FormEvent, useEffect, useMemo, useState } from "react";
import { Link, useNavigate } from "react-router-dom";
import { ApiError, api } from "../api/client";
import type { Branch } from "../types/api";
import { uniqueBranches } from "../utils/branches";

interface Props {
  onClose: () => void;
  initialBranch?: string;
}

export function NewRunForm({ onClose, initialBranch }: Props) {
  const navigate = useNavigate();
  const [scenarios, setScenarios] = useState<string[]>([]);
  const [autopilots, setAutopilots] = useState<string[]>([]);
  const [branches, setBranches] = useState<Branch[]>([]);
  const [scenario, setScenario] = useState("");
  const [autopilot, setAutopilot] = useState("baseline");
  const [branch, setBranch] = useState(initialBranch ?? "");
  const [loading, setLoading] = useState(true);
  const [branchesLoading, setBranchesLoading] = useState(true);
  const [runtimeConfigured, setRuntimeConfigured] = useState(false);
  const [configurationError, setConfigurationError] = useState<string | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [submitting, setSubmitting] = useState(false);

  useEffect(() => {
    let active = true;
    Promise.all([
      api.scenarios(),
      api.autopilots(),
      api.runtimeRepository(),
      api.runtimeBranches(),
    ])
      .then(([scenarioItems, autopilotItems, runtimeRepository, branchItems]) => {
        if (!active) return;
        setScenarios(scenarioItems);
        setScenario(scenarioItems[0] ?? "");
        setAutopilots(autopilotItems);
        setAutopilot(autopilotItems.includes("baseline") ? "baseline" : autopilotItems[0] ?? "");
        const options = uniqueBranches(branchItems, true);
        setBranches(options);
        const preferred = [initialBranch, runtimeRepository.default_branch, "backend", "main"]
          .find((name) => name && options.some((item) => item.name === name));
        setBranch(preferred ?? options[0]?.name ?? "");
        setRuntimeConfigured(true);
      })
      .catch((reason: unknown) => {
        if (!active) return;
        const message = reason instanceof Error ? reason.message : "Could not load New Run configuration";
        if (reason instanceof ApiError && reason.status === 503) {
          setConfigurationError("JSB0 Runtime repository is not configured.");
        } else {
          setError(message);
        }
      })
      .finally(() => {
        if (active) {
          setLoading(false);
          setBranchesLoading(false);
        }
      });
    return () => { active = false; };
  }, [initialBranch]);

  const branchHead = useMemo(
    () => branches.find((item) => item.name === branch)?.commit_sha ?? null,
    [branch, branches],
  );

  async function submit(event: FormEvent) {
    event.preventDefault();
    if (!runtimeConfigured || !branch) return;
    setSubmitting(true);
    setError(null);
    try {
      const run = await api.createRun({ scenario, autopilot, branch });
      navigate(`/runs/${run.id}`);
    } catch (reason) {
      setError(reason instanceof Error ? reason.message : "Could not create run");
      setSubmitting(false);
    }
  }

  return (
    <Dialog className="console-dialog" icon={IconNames.AIRPLANE} isOpen onClose={onClose} title="New simulation run">
      <form onSubmit={submit}>
        <DialogBody>
          <p className="dialog-intro">Select a JSB0 branch; the backend pins its immutable revision and resolves the build.</p>
          <FormGroup label="Scenario" labelFor="run-scenario">
            <HTMLSelect id="run-scenario" fill value={scenario} onChange={(event) => setScenario(event.currentTarget.value)} required disabled={loading}>
              {scenarios.map((item) => <option key={item}>{item}</option>)}
            </HTMLSelect>
          </FormGroup>
          {scenarios.length === 0 && !error && !loading && <Callout compact>No YAML scenarios were found.</Callout>}
          <FormGroup label="Autopilot" labelFor="run-autopilot">
            <HTMLSelect id="run-autopilot" fill value={autopilot} onChange={(event) => setAutopilot(event.currentTarget.value)} required disabled={loading}>
              {autopilots.map((item) => <option key={item}>{item}</option>)}
            </HTMLSelect>
          </FormGroup>
          <FormGroup label="JSB0 Branch" labelFor="run-branch" helperText={branchesLoading ? <span className="inline-loading"><Spinner size={12} /> Loading branches</span> : undefined}>
            <HTMLSelect id="run-branch" fill value={branch} onChange={(event) => setBranch(event.currentTarget.value)} required disabled={!runtimeConfigured || branchesLoading || branches.length === 0}>
              {branches.map((item) => <option key={item.name} value={item.name}>{item.name}</option>)}
            </HTMLSelect>
          </FormGroup>
          {!branchesLoading && runtimeConfigured && branches.length === 0 && !error && <Callout compact intent={Intent.WARNING}>No JSB0 branches are available.</Callout>}
          <FormGroup label="Resolved revision" helperText="Preview only. The backend resolves the branch again when the run is queued.">
            <div className="revision-preview" aria-label="Resolved revision" aria-live="polite"><code title={branchHead ?? undefined}>{branchHead?.slice(0, 12) ?? "—"}</code></div>
          </FormGroup>
          {configurationError && <Callout compact intent={Intent.DANGER} role="alert">{configurationError} <Link to="/repositories">Open Repositories</Link></Callout>}
          {error && <Callout compact intent={Intent.DANGER} role="alert">{error}</Callout>}
        </DialogBody>
        <DialogFooter actions={<>
          <Button type="button" onClick={onClose}>Cancel</Button>
          <Button intent={Intent.PRIMARY} loading={submitting} type="submit" disabled={loading || branchesLoading || !runtimeConfigured || !scenario || !autopilot || !branch}>
            Run simulation
          </Button>
        </>} />
      </form>
    </Dialog>
  );
}
