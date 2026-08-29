import { cleanup, fireEvent, render, screen } from "@testing-library/react";
import { afterEach, describe, expect, it, vi } from "vitest";
import { MemoryRouter, useLocation } from "react-router-dom";
import { AppShell } from "./AppShell";
import { ConfirmAction } from "./ConfirmAction";
import { DeploymentRevision, metadataMatches } from "./DeploymentRevision";
import { NewRunForm } from "./NewRunForm";
import { StatusTag } from "./StatusTag";

afterEach(() => {
  cleanup();
  vi.unstubAllGlobals();
});

function LocationProbe() {
  return <output aria-label="Current path">{useLocation().pathname}</output>;
}

describe("AppShell", () => {
  it("keeps client-side navigation semantics", () => {
    vi.stubGlobal("fetch", vi.fn().mockResolvedValue({
      ok: true,
      json: async () => ({ branch: "dev", commit: "unknown", short_commit: "unknown", built_at: "unknown", hostname: null }),
    }));
    render(
      <MemoryRouter initialEntries={["/runs"]}>
        <AppShell><LocationProbe /></AppShell>
      </MemoryRouter>,
    );

    expect(screen.getByRole("navigation", { name: "Primary navigation" })).toBeInTheDocument();
    fireEvent.click(screen.getByRole("menuitem", { name: "Deployments" }));
    expect(screen.getByLabelText("Current path")).toHaveTextContent("/deployments");
  });
});

describe("DeploymentRevision", () => {
  const info = {
    branch: "impl",
    commit: "8efb0664ab92f2df6155281415fbe33051366868",
    shortCommit: "8efb066",
    builtAt: "2026-08-29T04:42:00Z",
    hostname: "impl-jsb.mangagaki.net",
  };

  it("links the deployed commit and exposes full metadata in a tooltip", async () => {
    render(<DeploymentRevision info={info} />);
    const revision = screen.getByRole("link", { name: "Deployed revision impl @ 8efb066" });

    expect(revision).toHaveAttribute(
      "href",
      "https://github.com/egod1537/jsb1/commit/8efb0664ab92f2df6155281415fbe33051366868",
    );
    expect(revision).toHaveAttribute("target", "_blank");
    fireEvent.mouseEnter(revision);
    expect(await screen.findByText(`Commit: ${info.commit}`)).toBeInTheDocument();
    expect(screen.getByText(`Host: ${info.hostname}`)).toBeInTheDocument();
  });

  it("renders development metadata without a GitHub link", () => {
    render(<DeploymentRevision info={{ branch: "dev", commit: "unknown", shortCommit: "unknown", builtAt: "unknown" }} />);

    expect(screen.getByText("dev")).toBeInTheDocument();
    expect(screen.queryByRole("link")).not.toBeInTheDocument();
  });

  it("detects frontend/backend revision mismatches", () => {
    expect(metadataMatches(info, {
      branch: "impl",
      commit: "9ae3eee902040000000000000000000000000000",
      short_commit: "9ae3eee",
      built_at: info.builtAt,
      hostname: info.hostname,
    })).toBe(false);
  });
});

describe("StatusTag", () => {
  it("renders operational status text", () => {
    render(<StatusTag status="running" />);
    expect(screen.getByText("running")).toBeInTheDocument();
  });
});

describe("ConfirmAction", () => {
  it("requires an explicit destructive confirmation", () => {
    const onConfirm = vi.fn();
    render(
      <ConfirmAction
        isOpen
        title="Stop impl?"
        message="The preview will be unavailable."
        confirmLabel="Stop deployment"
        onCancel={() => undefined}
        onConfirm={onConfirm}
      />,
    );

    fireEvent.click(screen.getByRole("button", { name: "Stop deployment" }));
    expect(onConfirm).toHaveBeenCalledOnce();
  });
});

describe("NewRunForm", () => {
  it("exposes Blueprint dialog fields by label", async () => {
    vi.stubGlobal("fetch", vi.fn().mockResolvedValue({ ok: true, json: async () => [] }));
    render(<MemoryRouter><NewRunForm onClose={() => undefined} /></MemoryRouter>);

    expect(screen.getByRole("dialog", { name: "New simulation run" })).toBeInTheDocument();
    expect(await screen.findByLabelText("Scenario")).toBeInTheDocument();
    expect(screen.getByLabelText("Autopilot")).toBeInTheDocument();
    expect(screen.getByLabelText("Completed build")).toBeInTheDocument();
  });
});
