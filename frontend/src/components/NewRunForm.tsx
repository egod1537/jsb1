import { FormEvent, useEffect, useState } from "react";
import { useNavigate } from "react-router-dom";
import { api } from "../api/client";

export function NewRunForm({ onClose }: { onClose: () => void }) {
  const navigate = useNavigate();
  const [scenarios, setScenarios] = useState<string[]>([]);
  const [autopilots, setAutopilots] = useState<string[]>([]);
  const [scenario, setScenario] = useState("");
  const [autopilot, setAutopilot] = useState("primary");
  const [commitSha, setCommitSha] = useState("");
  const [error, setError] = useState<string | null>(null);
  const [submitting, setSubmitting] = useState(false);

  useEffect(() => {
    Promise.all([api.scenarios(), api.autopilots()])
      .then(([scenarioItems, autopilotItems]) => {
        setScenarios(scenarioItems);
        setScenario(scenarioItems[0] ?? "");
        setAutopilots(autopilotItems);
        setAutopilot(autopilotItems[0] ?? "");
      })
      .catch((reason: Error) => setError(reason.message));
  }, []);

  async function submit(event: FormEvent) {
    event.preventDefault();
    setSubmitting(true);
    setError(null);
    try {
      const run = await api.createRun({ scenario, autopilot, commit_sha: commitSha });
      navigate(`/runs/${run.id}`);
    } catch (reason) {
      setError(reason instanceof Error ? reason.message : "Could not create run");
      setSubmitting(false);
    }
  }

  return (
    <div className="modal-backdrop" role="presentation" onMouseDown={onClose}>
      <form className="new-run" onSubmit={submit} onMouseDown={(event) => event.stopPropagation()}>
        <div className="section-heading">
          <div>
            <span className="eyebrow">Simulation queue</span>
            <h2>New run</h2>
          </div>
          <button type="button" className="icon-button" onClick={onClose} aria-label="Close">×</button>
        </div>
        <label>
          Scenario
          <select value={scenario} onChange={(event) => setScenario(event.target.value)} required>
            {scenarios.map((item) => <option key={item}>{item}</option>)}
          </select>
        </label>
        {scenarios.length === 0 && !error && <p className="muted">No YAML scenarios were found.</p>}
        <label>
          Autopilot
          <select value={autopilot} onChange={(event) => setAutopilot(event.target.value)} required>
            {autopilots.map((item) => <option key={item}>{item}</option>)}
          </select>
        </label>
        <label>
          Commit SHA
          <input
            value={commitSha}
            onChange={(event) => setCommitSha(event.target.value)}
            placeholder="abc1234"
            pattern="[0-9a-fA-F]+"
            required
          />
        </label>
        {error && <p className="form-error" role="alert">{error}</p>}
        <div className="form-actions">
          <button type="button" className="button button--quiet" onClick={onClose}>Cancel</button>
          <button className="button" disabled={submitting || !scenario || !autopilot}>
            {submitting ? "Queuing…" : "Run simulation"}
          </button>
        </div>
      </form>
    </div>
  );
}
