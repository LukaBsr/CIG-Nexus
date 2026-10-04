import { describe, expect, it } from "vitest";

import { DEFAULT_THEME_ID, resolveThemeId, THEMES } from "./themes";

describe("resolveThemeId", () => {
  it("returns a known theme id unchanged", () => {
    expect(resolveThemeId("ember")).toBe("ember");
  });

  it("falls back to the default for an unknown id", () => {
    expect(resolveThemeId("not-a-real-theme")).toBe(DEFAULT_THEME_ID);
  });

  it("falls back to the default for null/undefined", () => {
    expect(resolveThemeId(null)).toBe(DEFAULT_THEME_ID);
    expect(resolveThemeId(undefined)).toBe(DEFAULT_THEME_ID);
  });

  it("DEFAULT_THEME_ID is itself a registered theme", () => {
    expect(THEMES.some((theme) => theme.id === DEFAULT_THEME_ID)).toBe(true);
  });

  for (const id of ["onyx", "mocha", "amethyst", "espresso"]) {
    it(`returns the dark-batch-1 id "${id}" unchanged`, () => {
      expect(resolveThemeId(id)).toBe(id);
    });
  }

  it("resolves every registered id to itself", () => {
    for (const theme of THEMES) {
      expect(resolveThemeId(theme.id)).toBe(theme.id);
    }
  });

  it("registers unique ids", () => {
    const ids = THEMES.map((theme) => theme.id);
    expect(new Set(ids).size).toBe(ids.length);
  });
});
