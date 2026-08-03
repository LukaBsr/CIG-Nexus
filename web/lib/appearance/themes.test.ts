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
});
