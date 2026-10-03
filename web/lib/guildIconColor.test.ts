import { describe, expect, it } from "vitest";

import { GUILD_ICON_FOREGROUND, guildIconBackground, hashHue } from "./guildIconColor";

// Independent of the production code on purpose: an HSL->sRGB conversion and
// WCAG relative-luminance math written out here, so the test checks the
// rendered contrast rather than re-deriving it from the same assumptions.
function hslToRgb(h: number, s: number, l: number): [number, number, number] {
  const c = (1 - Math.abs(2 * l - 1)) * s;
  const hp = h / 60;
  const x = c * (1 - Math.abs((hp % 2) - 1));
  const [r1, g1, b1] =
    hp < 1 ? [c, x, 0] : hp < 2 ? [x, c, 0] : hp < 3 ? [0, c, x] : hp < 4 ? [0, x, c] : hp < 5 ? [x, 0, c] : [c, 0, x];
  const m = l - c / 2;
  return [Math.round((r1 + m) * 255), Math.round((g1 + m) * 255), Math.round((b1 + m) * 255)];
}

function relativeLuminance([r, g, b]: [number, number, number]): number {
  const lin = (v: number) => {
    const s = v / 255;
    return s <= 0.03928 ? s / 12.92 : ((s + 0.055) / 1.055) ** 2.4;
  };
  return 0.2126 * lin(r) + 0.7152 * lin(g) + 0.0722 * lin(b);
}

function contrastRatio(a: [number, number, number], b: [number, number, number]): number {
  const la = relativeLuminance(a);
  const lb = relativeLuminance(b);
  return (Math.max(la, lb) + 0.05) / (Math.min(la, lb) + 0.05);
}

const foreground: [number, number, number] = [0xed, 0xef, 0xfb];

// Parses the string the production code actually emits, so the sweep checks
// what ships rather than a constant copied into the test.
function parseHsl(css: string): [number, number, number] {
  const match = /^hsl\((\d+), (\d+)%, (\d+)%\)$/.exec(css);
  if (!match) {
    throw new Error(`unexpected background format: ${css}`);
  }
  return [Number(match[1]), Number(match[2]) / 100, Number(match[3]) / 100];
}

describe("guildIconBackground", () => {
  it("keeps the initials at >= 4.5:1 against the shipped background for every hue", () => {
    for (let hue = 0; hue < 360; hue++) {
      const [h, s, l] = parseHsl(guildIconBackground(hue));
      expect(contrastRatio(foreground, hslToRgb(h, s, l)), `hue ${hue}`).toBeGreaterThanOrEqual(4.5);
    }
  });

  it("uses 30% lightness (the 32% value failed AA at hue 60)", () => {
    expect(guildIconBackground(60)).toBe("hsl(60, 45%, 30%)");
    expect(contrastRatio(foreground, hslToRgb(60, 0.45, 0.32))).toBeLessThan(4.5);
  });

  it("keeps the initials color a fixed literal, independent of any theme", () => {
    expect(GUILD_ICON_FOREGROUND).toBe("#edeffb");
  });
});

describe("hashHue", () => {
  it("maps any guild id to a hue in [0, 360)", () => {
    for (const id of ["g_1", "g_00000000-0000-4000-8000-000000000000", "", "a-very-long-id-".repeat(20)]) {
      const hue = hashHue(id);
      expect(Number.isInteger(hue)).toBe(true);
      expect(hue).toBeGreaterThanOrEqual(0);
      expect(hue).toBeLessThan(360);
    }
  });

  it("is deterministic for the same id", () => {
    expect(hashHue("g_abc")).toBe(hashHue("g_abc"));
  });
});
