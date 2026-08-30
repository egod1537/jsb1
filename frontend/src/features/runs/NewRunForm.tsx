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
  Tag,
  Tooltip,
} from "@blueprintjs/core";
import { IconNames } from "@blueprintjs/icons";
import { FormEvent, useCallback, useEffect, useMemo, useState } from "react";
import { Link, useLocation, useNavigate, useSearchParams } from "react-router-dom";
import { ApiError, api } from "../../api/client";
import type { Branch, ControllerParameterDefinition, ScenarioCatalogEntry, ScenarioSyncStatus } from "../../types/api";
import { uniqueBranches } from "../../utils/branches";
import { ScenarioViewerDialog } from "../scenarios/ScenarioViewerDialog";
import { controllerParameterErrors } from "../parameters/ControllerParameterEditor";
import { ControllerParameterConfigureDialog } from "../parameters/ControllerParameterConfigureDialog";

interface Props {
  onClose: () => void;
  initialBranch?: string;
}

export function NewRunForm({ onClose, initialBranch }: Props) {
  const navigate = useNavigate();
  const location = useLocation();
  const [searchParams] = useSearchParams();
  const requestedScenarioId = searchParams.get("scenario");
  const initialControllerParameters = (location.state as { controllerParameters?: Record<string, number> } | null)?.controllerParameters ?? {};
  const [scenarios, setScenarios] = useState<ScenarioCatalogEntry[]>([]);
  const [branches, setBranches] = useState<Branch[]>([]);
  const [scenario, setScenario] = useState("");
  const [branch, setBranch] = useState(initialBranch ?? "");
  const [loading, setLoading] = useState(true);
  const [branchesLoading, setBranchesLoading] = useState(true);
  const [runtimeConfigured, setRuntimeConfigured] = useState(false);
  const [configurationError, setConfigurationError] = useState<string | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [submitting, setSubmitting] = useState(false);
  const [syncing, setSyncing] = useState(false);
  const [syncStatus, setSyncStatus] = useState<ScenarioSyncStatus | null>(null);
  const [variants, setVariants] = useState<string[]>([]);
  const [headlessMode, setHeadlessMode] = useState("");
  const [variantsLoading, setVariantsLoading] = useState(false);
  const [variantError, setVariantError] = useState<string | null>(null);
  const [branchPreviewCommit, setBranchPreviewCommit] = useState<string | null>(null);
  const [scenarioViewerOpen, setScenarioViewerOpen] = useState(false);
  const [parameterDefinitions, setParameterDefinitions] = useState<ControllerParameterDefinition[]>([]);
  const [controllerParameters, setControllerParameters] = useState<Record<string, number>>(initialControllerParameters);
  const [controllerParameterDraft, setControllerParameterDraft] = useState<Record<string, number>>({});
  const [parametersDialogOpen, setParametersDialogOpen] = useState(false);
  const [parametersLoading, setParametersLoading] = useState(false);
  const [parametersError, setParametersError] = useState<string | null>(null);
  const [parametersResolvedBranch, setParametersResolvedBranch] = useState<string | null>(null);

  useEffect(() => {
    let active = true;
    Promise.all([
      api.scenarios(),
      api.runtimeRepository(),
      api.runtimeBranches(),
      api.scenarioSyncStatus(),
    ])
      .then(([scenarioItems, runtimeRepository, branchItems, remoteStatus]) => {
        if (!active) return;
        setScenarios(scenarioItems);
        setScenario(requestedScenarioId && scenarioItems.some((item) => `${item.source}:${item.id}` === requestedScenarioId)
          ? requestedScenarioId
          : scenarioItems[0] ? `${scenarioItems[0].source}:${scenarioItems[0].id}` : "");
        setSyncStatus(remoteStatus);
        const options = uniqueBranches(branchItems, true);
        setBranches(options);
        const preferred = [initialBranch, runtimeRepository.default_branch, "impl", "main"]
          .find((name) => name && options.some((item) => item.name === name));
        const selectedBranch = preferred ?? options[0]?.name ?? "";
        setBranch(selectedBranch);
        setVariantsLoading(Boolean(selectedBranch));
        setRuntimeConfigured(true);
      })
      .catch((reason: unknown) => {
        if (!active) return;
        const message = reason instanceof Error ? reason.message : "Could not load New Run configuration";
        if (reason instanceof ApiError && reason.status === 503) {
          setConfigurationError(message.endsWith(".") ? message : `${message}.`);
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
  }, [initialBranch, requestedScenarioId]);

  useEffect(() => {
    if (!branch || !runtimeConfigured) return;
    let active = true;
    setVariantsLoading(true);
    setParametersLoading(true);
    setVariantError(null);
    setParametersError(null);
    setParametersResolvedBranch(null);
    setBranchPreviewCommit(null);
    Promise.allSettled([api.runtimeVariants(branch), api.runtimeParameters(branch)])
      .then(([capabilityResult, parameterResult]) => {
        if (!active) return;
        let supported: string[] = [];
        if (capabilityResult.status === "fulfilled") {
          const capability = capabilityResult.value;
          supported = Array.isArray(capability.variants) ? capability.variants : [];
          if (capability.mode !== "compare" || supported.length < 2) {
            setVariants([]);
            setHeadlessMode("");
            setVariantError("Selected JSB0 revision is not compare-only headless capable");
          } else {
            setVariants(supported);
            setHeadlessMode(capability.mode);
            setBranchPreviewCommit(capability.commit_sha);
          }
        } else {
          setVariants([]);
          setHeadlessMode("");
          setVariantError(capabilityResult.reason instanceof Error
            ? capabilityResult.reason.message
            : "Could not load execution variants");
        }

        if (parameterResult.status === "fulfilled") {
          const definitions = parameterResult.value.parameters ?? [];
          setParameterDefinitions(definitions);
          setParametersResolvedBranch(branch);
        } else {
          setParameterDefinitions([]);
          setParametersError(parameterResult.reason instanceof Error
            ? parameterResult.reason.message
            : "Could not load controller parameters");
        }
      })
      .finally(() => {
        if (active) {
          setVariantsLoading(false);
          setParametersLoading(false);
        }
      });
    return () => { active = false; };
  }, [branch, runtimeConfigured]);

  const branchHead = useMemo(
    () => branches.find((item) => item.name === branch)?.commit_sha ?? null,
    [branch, branches],
  );
  const selectedBranchPreview = variantsLoading
    ? "resolving…"
    : variantError
      ? "unavailable"
      : (branchPreviewCommit ?? branchHead)?.slice(0, 7) ?? "unavailable";
  const branchPreviewTitle = branchPreviewCommit
    ? `${branchPreviewCommit}\nPreview; resolved again when run is queued`
    : "Preview; resolved again when run is queued";

  const selectedScenario = useMemo(
    () => scenarios.find((item) => `${item.source}:${item.id}` === scenario) ?? null,
    [scenario, scenarios],
  );
  const requestedParameterIds = useMemo(
    () => selectedScenario?.controller_parameters ?? [],
    [selectedScenario],
  );
  const allowedParameterDefinitions = useMemo(() => {
    const definitions = new Map(parameterDefinitions
      .filter((item) => (item.variants ?? []).length === 0 || item.variants.some((value) => variants.includes(value)))
      .map((item) => [item.id, item]));
    return requestedParameterIds.flatMap((id) => {
      const definition = definitions.get(id);
      return definition ? [definition] : [];
    });
  }, [parameterDefinitions, requestedParameterIds, variants]);
  const unsupportedParameterIds = useMemo(() => {
    if (
      parametersResolvedBranch !== branch
      || variantsLoading
      || variantError
      || headlessMode !== "compare"
    ) return [];
    const supported = new Set(allowedParameterDefinitions.map((item) => item.id));
    return requestedParameterIds.filter((id) => !supported.has(id));
  }, [
    allowedParameterDefinitions,
    branch,
    headlessMode,
    parametersResolvedBranch,
    requestedParameterIds,
    variantError,
    variantsLoading,
  ]);
  const parameterValidationErrors = useMemo(
    () => variantError || headlessMode !== "compare"
      ? []
      : controllerParameterErrors(allowedParameterDefinitions, controllerParameters, variants),
    [allowedParameterDefinitions, controllerParameters, headlessMode, variantError, variants],
  );
  useEffect(() => {
    if (parametersResolvedBranch !== branch) return;
    setControllerParameters((current) => Object.fromEntries(
      allowedParameterDefinitions.map((item) => [
        item.id,
        Number.isFinite(current[item.id]) ? current[item.id] : item.default_value,
      ]),
    ));
    setParametersDialogOpen(false);
  }, [allowedParameterDefinitions, branch, parametersResolvedBranch, scenario]);
  const loadSelectedScenario = useCallback(() => {
    if (!selectedScenario) return Promise.reject(new Error("No scenario selected"));
    return api.scenarioDetail(selectedScenario.source, selectedScenario.id);
  }, [selectedScenario]);

  function openControllerParameters() {
    setControllerParameterDraft(Object.fromEntries(
      allowedParameterDefinitions.map((item) => [
        item.id,
        Number.isFinite(controllerParameters[item.id])
          ? controllerParameters[item.id]
          : item.default_value,
      ]),
    ));
    setParametersDialogOpen(true);
  }

  function applyControllerParameters() {
    setControllerParameters(Object.fromEntries(
      allowedParameterDefinitions.map((item) => [
        item.id,
        controllerParameterDraft[item.id] ?? item.default_value,
      ]),
    ));
    setParametersDialogOpen(false);
  }

  async function syncRemote() {
    setSyncing(true);
    setError(null);
    try {
      const result = await api.syncScenarios();
      const [scenarioItems, remoteStatus] = await Promise.all([
        api.scenarios(),
        api.scenarioSyncStatus(),
      ]);
      setScenarios(scenarioItems);
      setSyncStatus(remoteStatus);
      if (!result.reachable) setError(result.error ?? "Could not sync remote scenarios");
    } catch (reason) {
      setError(reason instanceof Error ? reason.message : "Could not sync remote scenarios");
    } finally {
      setSyncing(false);
    }
  }

  async function submit(event: FormEvent) {
    event.preventDefault();
    if (!runtimeConfigured || !branch) return;
    setSubmitting(true);
    setError(null);
    try {
      if (!selectedScenario) return;
      const executionParameters = Object.fromEntries(allowedParameterDefinitions
        .map((item) => [item.id, controllerParameters[item.id] ?? item.default_value]));
      const run = await api.createRun({
        scenario: selectedScenario.id,
        scenario_source: selectedScenario.source,
        branch,
        ...(Object.keys(executionParameters).length > 0
          ? { controller_parameters: executionParameters }
          : {}),
      });
      navigate(`/runs/${run.id}`);
    } catch (reason) {
      setError(reason instanceof Error ? reason.message : "Could not create run");
      setSubmitting(false);
    }
  }

  return (
    <Dialog className="console-dialog new-run-dialog" icon={IconNames.AIRPLANE} isOpen onClose={onClose} title="New simulation run">
      <form onSubmit={submit}>
        <DialogBody>
          <p className="dialog-intro">Select a JSB0 branch; the backend pins its immutable revision and resolves the build.</p>
          <FormGroup label="Scenario" labelFor="run-scenario">
            <div className="new-run-scenario-row">
              <HTMLSelect id="run-scenario" fill value={scenario} onChange={(event) => setScenario(event.currentTarget.value)} required disabled={loading}>
                {scenarios.map((item) => <option key={`${item.source}:${item.id}`} value={`${item.source}:${item.id}`}>{item.name} · {item.source}</option>)}
              </HTMLSelect>
              <Button disabled={!selectedScenario} icon={IconNames.EYE_OPEN} onClick={() => setScenarioViewerOpen(true)} small type="button">View</Button>
            </div>
          </FormGroup>
          {selectedScenario && <div className="scenario-source-summary">
            <span>{selectedScenario.scenario_type && <><code>{selectedScenario.scenario_type}</code>{" · "}</>}{selectedScenario.schema_version != null && <>schema v{selectedScenario.schema_version}{" · "}</>}<Tag minimal>{selectedScenario.source.toUpperCase()}</Tag>{" "}
              <Tooltip content={`SHA-256: ${selectedScenario.scenario_sha256}`}>
                <Tag minimal intent={Intent.SUCCESS}>VALID</Tag>
              </Tooltip></span>
          </div>}
          {syncStatus?.configured && <div className="scenario-sync-row">
            <Button small minimal icon={IconNames.REFRESH} loading={syncing} onClick={syncRemote} type="button">Sync remote scenarios</Button>
            <span>{syncStatus.reachable === false ? "Last sync failed; using cached scenarios" : syncStatus.last_success_at ? `Last synced ${new Date(syncStatus.last_success_at).toLocaleString()}` : "Not synced yet"}</span>
          </div>}
          {scenarios.length === 0 && !error && !loading && <Callout compact>No YAML scenarios were found.</Callout>}
          <FormGroup label="JSB0 Branch" labelFor="run-branch" helperText={branchesLoading ? <span className="inline-loading"><Spinner size={12} /> Loading branches</span> : undefined}>
            <HTMLSelect aria-busy={variantsLoading} className="new-run-branch-select" id="run-branch" fill title={branchPreviewTitle} value={branch} onChange={(event) => {
              setBranchPreviewCommit(null);
              setVariantError(null);
              setVariantsLoading(true);
              setParametersLoading(true);
              setParametersResolvedBranch(null);
              setBranch(event.currentTarget.value);
            }} required disabled={!runtimeConfigured || branchesLoading || branches.length === 0}>
              {branches.map((item) => <option key={item.name} value={item.name}>
                {item.name} · {item.name === branch ? selectedBranchPreview : item.commit_sha.slice(0, 7)}
              </option>)}
            </HTMLSelect>
          </FormGroup>
          {!branchesLoading && runtimeConfigured && branches.length === 0 && !error && <Callout compact intent={Intent.WARNING}>No JSB0 branches are available.</Callout>}
          {!variantsLoading && headlessMode === "compare" && <Callout compact intent={Intent.PRIMARY}>
            This revision runs {variants.join(" + ")} together in one Run.
          </Callout>}
          {variantError && <Callout compact intent={Intent.DANGER} role="alert">{variantError}</Callout>}
          {requestedParameterIds.length > 0 && <section className="new-run-parameter-summary" aria-label="Controller Parameters">
            <div><strong>Controller Parameters</strong><small>{requestedParameterIds.length} parameters</small></div>
            <Button
              disabled={
                variantsLoading
                || Boolean(variantError)
                || parametersLoading
                || Boolean(parametersError)
                || allowedParameterDefinitions.length === 0
              }
              icon={IconNames.PROPERTIES}
              onClick={openControllerParameters}
              small
              type="button"
            >Configure</Button>
          </section>}
          {requestedParameterIds.length > 0 && parametersError && <Callout compact intent={Intent.DANGER} role="alert">{parametersError}</Callout>}
          {unsupportedParameterIds.length > 0 && <Callout compact intent={Intent.DANGER} role="alert">
            Unsupported controller parameter for selected JSB0 revision: {unsupportedParameterIds.join(", ")}
          </Callout>}
          {parameterValidationErrors.length > 0 && <Callout compact intent={Intent.DANGER}>{parameterValidationErrors.join("; ")}</Callout>}
          {configurationError && <Callout compact intent={Intent.DANGER} role="alert">{configurationError} <Link to="/settings">Open Settings</Link></Callout>}
          {error && <Callout compact intent={Intent.DANGER} role="alert">{error}</Callout>}
        </DialogBody>
        <DialogFooter actions={<>
          <Button type="button" onClick={onClose}>Cancel</Button>
          <Button intent={Intent.PRIMARY} loading={submitting} type="submit" disabled={loading || branchesLoading || variantsLoading || (requestedParameterIds.length > 0 && (parametersLoading || Boolean(parametersError))) || unsupportedParameterIds.length > 0 || parameterValidationErrors.length > 0 || !runtimeConfigured || !selectedScenario || !branch || headlessMode !== "compare"}>Run simulation</Button>
        </>} />
      </form>
      {selectedScenario && <ScenarioViewerDialog
        isOpen={scenarioViewerOpen}
        load={loadSelectedScenario}
        onClose={() => setScenarioViewerOpen(false)}
        title={selectedScenario.name}
      />}
      <ControllerParameterConfigureDialog
        definitions={allowedParameterDefinitions}
        isOpen={parametersDialogOpen}
        onApply={applyControllerParameters}
        onCancel={() => setParametersDialogOpen(false)}
        onChange={setControllerParameterDraft}
        values={controllerParameterDraft}
        variants={variants}
      />
    </Dialog>
  );
}
