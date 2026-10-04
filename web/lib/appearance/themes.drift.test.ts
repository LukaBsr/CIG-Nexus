import { describe, expect, it } from "vitest";

import { THEMES } from "./themes";
import {
  declaredThemeIds,
  effectiveColorScheme,
  effectiveTokens,
  hexToRgb,
  rawBlock,
  readWebFile,
  TOKEN_NAMES,
} from "./themeStylesheet";

// Keeps the registry (themes.ts), the stylesheet (globals.css) and the two
// hard-coded profile accents in step. A failure here means one of those
// drifted from the others, not that a theme is wrong by itself.
describe("theme registry matches globals.css", () => {
  for (const theme of THEMES) {
    describe(theme.id, () => {
      it("has a CSS block (abyss is the @theme default)", () => {
        expect(rawBlock(theme.id), `no [data-theme="${theme.id}"] block`).not.toBeNull();
      });

      it("defines all eleven tokens once the cascade is applied", () => {
        const tokens = effectiveTokens(theme.id);
        for (const name of TOKEN_NAMES) {
          expect(tokens, `--color-${name}`).toHaveProperty(name);
        }
      });

      it(`color-scheme matches mode "${theme.mode}"`, () => {
        expect(effectiveColorScheme(theme.id)).toBe(theme.mode);
      });

      it("swatch accent and secondary equal the block's brand and brand-2", () => {
        const tokens = effectiveTokens(theme.id);
        expect(tokens.brand).toBe(theme.swatch.accent);
        expect(tokens["brand-2"]).toBe(theme.swatch.secondary);
      });
    });
  }

  it("every [data-theme] block in globals.css is registered", () => {
    const registered = new Set(THEMES.map((theme) => theme.id));
    for (const id of declaredThemeIds()) {
      expect(registered.has(id), `orphan block [data-theme="${id}"]`).toBe(true);
    }
  });

  it("the profile accent fallbacks equal Abyss's brand", () => {
    const abyssBrand = effectiveTokens("abyss").brand;
    expect(hexToRgb(abyssBrand)).not.toBeNull();
    for (const file of [
      "components/ProfileView.tsx",
      "components/settings/ProfileSettings.tsx",
    ]) {
      const match = /const DEFAULT_ACCENT = "(#[0-9a-fA-F]{6})";/.exec(readWebFile(file));
      expect(match, `DEFAULT_ACCENT not found in ${file}`).not.toBeNull();
      expect(match![1].toLowerCase(), file).toBe(abyssBrand.toLowerCase());
    }
  });
});
