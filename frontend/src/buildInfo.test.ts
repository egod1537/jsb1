import { describe, expect, it } from "vitest";
import { githubCommitUrl, parseBuildInfo } from "./buildInfo";

const FULL_COMMIT = "8efb0664ab92f2df6155281415fbe33051366868";

describe("buildInfo", () => {
  it("parses Vite deployment metadata and derives a short commit", () => {
    const info = parseBuildInfo({
      VITE_JSB1_BRANCH: "impl",
      VITE_JSB1_COMMIT: FULL_COMMIT,
      VITE_JSB1_BUILD_TIME: "2026-08-29T04:42:00Z",
      VITE_JSB1_HOSTNAME: "impl-jsb.mangagaki.net",
    });

    expect(info).toEqual({
      branch: "impl",
      commit: FULL_COMMIT,
      shortCommit: "8efb066",
      builtAt: "2026-08-29T04:42:00Z",
      hostname: "impl-jsb.mangagaki.net",
    });
    expect(githubCommitUrl(info)).toBe(`https://github.com/egod1537/jsb1/commit/${FULL_COMMIT}`);
  });

  it("uses safe development fallbacks without creating a commit link", () => {
    const info = parseBuildInfo({});

    expect(info).toEqual({
      branch: "dev",
      commit: "unknown",
      shortCommit: "unknown",
      builtAt: "unknown",
    });
    expect(githubCommitUrl(info)).toBeUndefined();
  });
});
