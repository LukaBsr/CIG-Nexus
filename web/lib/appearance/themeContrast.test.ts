import { describe, expect, it } from "vitest";

import { THEMES } from "./themes";
import { effectiveTokens, hexToRgb } from "./themeStylesheet";

// Targets from docs/design/brand-guidelines.md §7.1, applied to every
// registered theme's sRGB hex tokens. Non-hex values (oklch, used by Abyss's
// danger and online) are reported as skipped, not silently passed.
// Surfaces mirror tokens.test.ts; brand/90 is composited over page, the same
// backdrop the catalog's btn90 figure uses.

type RGB = [number, number, number];

function luminance([r, g, b]: RGB): number {
  const lin = (v: number) => {
    const s = v / 255;
    return s <= 0.03928 ? s / 12.92 : ((s + 0.055) / 1.055) ** 2.4;
  };
  return 0.2126 * lin(r) + 0.7152 * lin(g) + 0.0722 * lin(b);
}

function contrast(a: RGB, b: RGB): number {
  const la = luminance(a);
  const lb = luminance(b);
  return (Math.max(la, lb) + 0.05) / (Math.min(la, lb) + 0.05);
}

function over(fg: RGB, bg: RGB, alpha: number): RGB {
  return [0, 1, 2].map((i) => fg[i] * alpha + bg[i] * (1 - alpha)) as RGB;
}

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
