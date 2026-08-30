import { cleanup, fireEvent, render, screen, within } from "@testing-library/react";
import { afterEach, describe, expect, it } from "vitest";
import type { PipelineStage, PipelineStageStatus } from "../../types/api";
import {
  BUILD_PIPELINE_GROUPS,
  ExecutionPipeline,
  RUN_PIPELINE_GROUPS,
  groupPipelineStages,
  pipelineGroupStatus,
} from "./ExecutionPipeline";

function stage(
  id: string,
  label: string,
  status: PipelineStageStatus,
  overrides: Partial<PipelineStage> = {},
): PipelineStage {
  return {
    id,
    label,
    status,
    started_at: status === "pending" ? null : "2026-01-01T00:00:00Z",
    finished_at: status === "success" || status === "failed" || status === "skipped" ? "2026-01-01T00:00:01Z" : null,
    duration_sec: status === "success" || status === "failed" ? 1 : null,
    message: null,
    error: null,
    ...overrides,
  };
}

const runStages: PipelineStage[] = [
  stage("resolve_scenario", "Resolve Scenario", "success"),
  stage("resolve_runtime_revision", "Resolve Runtime Revision", "success"),
  stage("validate_contract", "Validate Contract", "success"),
  stage("resolve_build", "Resolve Build", "success"),
  stage("freeze_scenario", "Freeze Scenario", "success"),
  stage("launch_runner", "Launch Runner", "running", { message: "Starting runner" }),
  stage("record_telemetry", "Record Telemetry", "pending"),
  stage("collect_artifacts", "Collect Artifacts", "pending"),
  stage("complete", "Complete", "pending"),
];

afterEach(cleanup);

describe("ExecutionPipeline", () => {
  it("renders at most six Run groups while keeping details collapsed", () => {
    render(<ExecutionPipeline stages={runStages} groups={RUN_PIPELINE_GROUPS} title="Run pipeline" />);
    const list = screen.getByRole("list", { name: "Run pipeline groups" });
    expect(list).toHaveClass("pipeline-scroll");
    expect(within(list).getAllByRole("listitem")).toHaveLength(6);
    expect(screen.getByRole("button", { name: "Prepare: success" })).toBeInTheDocument();
    expect(screen.getByRole("button", { name: "Build: success" })).toBeInTheDocument();
    expect(screen.getByRole("button", { name: "Execute: running" })).toBeInTheDocument();
    expect(screen.queryByRole("region", { name: "Prepare detailed stages" })).not.toBeInTheDocument();
    expect(screen.queryByText("Resolve Scenario")).not.toBeInTheDocument();
  });

  it("expands and collapses one group with all persisted raw metadata", () => {
    render(<ExecutionPipeline stages={runStages} groups={RUN_PIPELINE_GROUPS} title="Run pipeline" />);
    const execute = screen.getByRole("button", { name: "Execute: running" });
    fireEvent.click(execute);
    const details = screen.getByRole("region", { name: "Execute detailed stages" });
    expect(within(details).getByText("Launch Runner")).toBeInTheDocument();
    expect(within(details).getByText("Starting runner")).toBeInTheDocument();
    expect(within(details).getByText(/^Started /)).toBeInTheDocument();
    expect(execute).toHaveAttribute("aria-expanded", "true");
    fireEvent.click(execute);
    expect(screen.queryByRole("region", { name: "Execute detailed stages" })).not.toBeInTheDocument();
  });

  it("calculates failed, running, success, and pending group states", () => {
    expect(pipelineGroupStatus([stage("a", "A", "success"), stage("b", "B", "failed")])).toBe("failed");
    expect(pipelineGroupStatus([stage("a", "A", "success"), stage("b", "B", "running")])).toBe("running");
    expect(pipelineGroupStatus([stage("a", "A", "success"), stage("b", "B", "skipped")])).toBe("success");
    expect(pipelineGroupStatus([stage("a", "A", "success"), stage("b", "B", "pending")])).toBe("pending");
    expect(pipelineGroupStatus([])).toBe("pending");
  });

  it("maps Build stages into four groups and preserves unmapped raw stages", () => {
    const buildStages = [
      stage("fetch_repository", "Fetch Repository", "success"),
      stage("prepare_worktree", "Prepare Worktree", "success"),
      stage("configure", "Configure", "success"),
      stage("custom_build_probe", "Custom Build Probe", "success"),
      stage("compile", "Compile", "running"),
      stage("verify_artifact", "Verify Artifact", "pending"),
      stage("complete", "Complete", "pending"),
    ];
    const grouped = groupPipelineStages(buildStages, BUILD_PIPELINE_GROUPS);
    expect(grouped.map((group) => group.label)).toEqual(["Prepare", "Build", "Verify", "Complete"]);
    expect(grouped.find((group) => group.id === "build")?.status).toBe("running");
    expect(grouped.flatMap((group) => group.stages).map((item) => item.id)).toEqual(buildStages.map((item) => item.id));
  });

  it("renders no empty placeholder when a legacy entity has no stages", () => {
    const { container } = render(<ExecutionPipeline stages={[]} groups={RUN_PIPELINE_GROUPS} />);
    expect(container).toBeEmptyDOMElement();
  });
});
