import {
  Button,
  Callout,
  Dialog,
  DialogBody,
  DialogFooter,
  FormGroup,
  HTMLSelect,
  InputGroup,
  Intent,
} from "@blueprintjs/core";
import { IconNames } from "@blueprintjs/icons";
import { FormEvent, useEffect, useState } from "react";
import { useNavigate } from "react-router-dom";
import { api } from "../api/client";

export function NewRunForm({ onClose, initialBuildId }: { onClose: () => void; initialBuildId?: number }) {
  const navigate = useNavigate();
  const [scenarios, setScenarios] = useState<string[]>([]);
  const [autopilots, setAutopilots] = useState<string[]>([]);
  const [scenario, setScenario] = useState("");
  const [autopilot, setAutopilot] = useState("primary");
  const [commitSha, setCommitSha] = useState("");
  const [builds, setBuilds] = useState<Awaited<ReturnType<typeof api.builds>>>([]);
  const [buildId, setBuildId] = useState(initialBuildId ? String(initialBuildId) : "");
  const [error, setError] = useState<string | null>(null);
  const [submitting, setSubmitting] = useState(false);

  useEffect(() => {
    Promise.all([api.scenarios(), api.autopilots(), api.builds()])
      .then(([scenarioItems, autopilotItems, buildItems]) => {
        setScenarios(scenarioItems);
        setScenario(scenarioItems[0] ?? "");
        setAutopilots(autopilotItems);
        setAutopilot(autopilotItems[0] ?? "");
        setBuilds(buildItems.filter((item) => item.status === "completed"));
      })
      .catch((reason: Error) => setError(reason.message));
  }, []);

  async function submit(event: FormEvent) {
    event.preventDefault();
    setSubmitting(true);
    setError(null);
    try {
      const run = await api.createRun({
        scenario,
        autopilot,
        ...(buildId ? { build_id: Number(buildId) } : commitSha ? { commit_sha: commitSha } : {}),
      });
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
          <p className="dialog-intro">Queue an immutable scenario/autopilot combination for the regression runner.</p>
          <FormGroup label="Scenario" labelFor="run-scenario">
            <HTMLSelect id="run-scenario" fill value={scenario} onChange={(event) => setScenario(event.currentTarget.value)} required>
            {scenarios.map((item) => <option key={item}>{item}</option>)}
            </HTMLSelect>
          </FormGroup>
          {scenarios.length === 0 && !error && <Callout compact>No YAML scenarios were found.</Callout>}
          <FormGroup label="Autopilot" labelFor="run-autopilot">
            <HTMLSelect id="run-autopilot" fill value={autopilot} onChange={(event) => setAutopilot(event.currentTarget.value)} required>
            {autopilots.map((item) => <option key={item}>{item}</option>)}
            </HTMLSelect>
          </FormGroup>
          <FormGroup label="Completed build" labelFor="run-build">
            <HTMLSelect id="run-build" fill value={buildId} onChange={(event) => setBuildId(event.currentTarget.value)}>
              <option value="">Legacy configured runner</option>
              {builds.map((build) => <option key={build.id} value={build.id}>
                #{build.id} · {build.repository_name} · {build.branch ?? build.commit_sha.slice(0, 10)}
              </option>)}
            </HTMLSelect>
          </FormGroup>
          {!buildId && <FormGroup label="Commit SHA" labelFor="run-commit" helperText="Optional legacy metadata">
            <InputGroup
              id="run-commit"
              value={commitSha}
              onChange={(event) => setCommitSha(event.currentTarget.value)}
              placeholder="abc1234"
              pattern="[0-9a-fA-F]+"
            />
          </FormGroup>}
          {error && <Callout compact intent={Intent.DANGER} role="alert">{error}</Callout>}
        </DialogBody>
        <DialogFooter actions={<>
          <Button type="button" onClick={onClose}>Cancel</Button>
          <Button intent={Intent.PRIMARY} loading={submitting} type="submit" disabled={!scenario || !autopilot}>
            Run simulation
          </Button>
        </>} />
      </form>
    </Dialog>
  );
}
