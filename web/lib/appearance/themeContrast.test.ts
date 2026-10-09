import { describe, expect, it } from "vitest";

import { THEMES } from "./themes";
import { contrast, effectiveTokens, hexToRgb, over, type RGB } from "./themeStylesheet";

// Targets from docs/design/brand-guidelines.md §7.1, applied to every
// registered theme's sRGB hex tokens. Non-hex values are reported as skipped,
// not silently passed. That includes the oklch danger and online that Abyss
// declares, and the online that Ember inherits from it. Those two status
// colors are not verified here; their contrast is checked only through the
// charter's sRGB approximations (the danger/online hex values in
// docs/design/theme-catalog.json). Revisit this test when the status colors
// are touched.
// Surfaces mirror tokens.test.ts; brand/90 is composited over page, the same
// backdrop the catalog's btn90 figure uses. luminance/contrast/over live in
// themeStylesheet.ts, shared with dangerTintContrast.test.ts.

for (const theme of THEMES) {
  describe(`${theme.id} contrast`, () => {
    const tokens = effectiveTokens(theme.id);
    const rgb = (name: string): RGB | null => hexToRgb(tokens[name] ?? "");
    const surfaces = ["page", "surface", "raised"] as const;

    // Runs `check` when the token is hex; otherwise reports a visible skip.
    const checkIfHex = (
      name: string,
      check: (value: RGB) => void
    ) => {
      const value = rgb(name);
      it.skipIf(value === null)(`${name} is hex (oklch values are skipped)`, () => {
        check(value!);
      });
    };

    checkIfHex("fg", (fg) => {
      for (const bg of ["page", "surface"] as const) {
        expect(contrast(fg, rgb(bg)!), `fg on ${bg}`).toBeGreaterThanOrEqual(7);
      }
      expect(contrast(fg, rgb("raised")!), "fg on raised").toBeGreaterThanOrEqual(4.5);
    });

    for (const name of ["muted", "brand", "brand-2", "danger"]) {
      checkIfHex(name, (value) => {
        for (const bg of surfaces) {
          expect(contrast(value, rgb(bg)!), `${name} on ${bg}`).toBeGreaterThanOrEqual(4.5);
        }
      });
    }

    checkIfHex("brand", (brand) => {
      expect(contrast(rgb("page")!, brand), "page text on brand").toBeGreaterThanOrEqual(4.5);
    });

    checkIfHex("brand", (brand) => {
      const hover = over(brand, rgb("page")!, 0.9);
      expect(contrast(rgb("page")!, hover), "page text on brand/90").toBeGreaterThanOrEqual(4.5);
    });

    for (const name of ["warning", "online"]) {
      checkIfHex(name, (value) => {
        for (const bg of surfaces) {
          expect(contrast(value, rgb(bg)!), `${name} on ${bg}`).toBeGreaterThanOrEqual(3);
        }
      });
    }
  });
}
