/// <reference types="vite/client" />

import { describe, expect, it } from "vitest";

const featureSources = import.meta.glob(
  ["./features/**/*.{ts,tsx}", "!./features/**/*.test.{ts,tsx}"],
  { eager: true, import: "default", query: "?raw" },
) as Record<string, string>;

const pageSources = import.meta.glob(
  ["./pages/*.{ts,tsx}", "!./pages/*.test.{ts,tsx}"],
  { eager: true, import: "default", query: "?raw" },
) as Record<string, string>;

const frontendSources = import.meta.glob(
  ["./**/*.{ts,tsx}", "!./**/*.test.{ts,tsx}"],
  { eager: true, import: "default", query: "?raw" },
) as Record<string, string>;

describe("frontend architecture boundaries", () => {
  it("requires cross-feature imports to use the target feature public API", () => {
    const violations: string[] = [];
    for (const [path, source] of Object.entries(featureSources)) {
      const owner = path.split("/")[2];
      for (const match of source.matchAll(/from\s+["']\.\.\/(?!\.)([^/"']+)(\/[^"']+)?["']/g)) {
        const [, target, internalPath] = match;
        if (target !== owner && internalPath) violations.push(`${path} -> ${target}${internalPath}`);
      }
      if (/from\s+["']\.\.\/\.\.\/pages\//.test(source)) violations.push(`${path} -> pages`);
    }
    expect(violations).toEqual([]);
  });

  it("keeps pages as route-level feature exports", () => {
    for (const [path, source] of Object.entries(pageSources)) {
      expect(source.trim(), path).toMatch(/^export \{ \w+ as \w+ \} from "\.\.\/features\/\w+";$/);
    }
  });

  it("keeps browser transport in the API adapter", () => {
    const violations = Object.entries(frontendSources)
      .filter(([path, source]) => path !== "./api/client.ts" && /\bfetch\s*\(/.test(source))
      .map(([path]) => path);
    expect(violations).toEqual([]);
  });
});
