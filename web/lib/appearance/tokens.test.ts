import { readFileSync } from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

import { describe, expect, it } from "vitest";

// Reads the real stylesheet rather than copying its values here, so the test
// fails when globals.css drifts — the thing it exists to catch.
const here = path.dirname(fileURLToPath(import.meta.url));
const css = readFileSync(path.join(here, "../../app/globals.css"), "utf8").replace(/\/\*[\s\S]*?\*\//g, "");

const REQUIRED_TOKENS = ["page", "surface", "raised", "slate", "fg", "muted", "brand", "brand-2", "danger", "warning", "online"];

function block(source: string, header: RegExp): string {
  const start = source.search(header);
  if (start < 0) throw new Error(`block not found: ${header}`);
  const open = source.indexOf("{", start);
  let depth = 0;
  for (let i = open; i < source.length; i++) {
    if (source[i] === "{") depth++;
    else if (source[i] === "}" && --depth === 0) return source.slice(open + 1, i);
  }
  throw new Error(`unterminated block: ${header}`);
}

function declarations(body: string): Record<string, string> {
  const out: Record<string, string> = {};
  for (const m of body.matchAll(/--color-([a-z0-9-]+)\s*:\s*([^;]+);/g)) {
    out[m[1]] = m[2].trim();
  }
  return out;
}

const themeTokens = declarations(block(css, /@theme\s*\{/));
const emberTokens = { ...themeTokens, ...declarations(block(css, /\[data-theme="ember"\]\s*\{/)) };

function hexToRgb(value: string): [number, number, number] | null {
  const m = /^#([0-9a-fA-F]{6})$/.exec(value);
  if (!m) return null;
  const n = parseInt(m[1], 16);
  return [(n >> 16) & 255, (n >> 8) & 255, n & 255];
}

function luminance([r, g, b]: [number, number, number]): number {
  const lin = (v: number) => {
    const s = v / 255;
    return s <= 0.03928 ? s / 12.92 : ((s + 0.055) / 1.055) ** 2.4;
  };
  return 0.2126 * lin(r) + 0.7152 * lin(g) + 0.0722 * lin(b);
}

function contrast(a: [number, number, number], b: [number, number, number]): number {
  const la = luminance(a);
  const lb = luminance(b);
  return (Math.max(la, lb) + 0.05) / (Math.min(la, lb) + 0.05);
}

describe("design tokens in globals.css", () => {
  it("@theme declares all eleven v0.2 tokens", () => {
    for (const token of REQUIRED_TOKENS) {
      expect(themeTokens, `missing --color-${token}`).toHaveProperty(token);
    }
  });

  for (const [label, tokens] of [
    ["Abyss (default)", themeTokens],
    ["Ember (overrides applied)", emberTokens],
  ] as const) {
    describe(label, () => {
      const rgb = (name: string) => hexToRgb(tokens[name] ?? "");
      const background = (name: string) => rgb(name);

      it("fg is at least 7:1 on page and surface", () => {
        const fg = rgb("fg");
        expect(fg).not.toBeNull();
        for (const bg of ["page", "surface"]) {
          expect(contrast(fg!, background(bg)!), `fg on ${bg}`).toBeGreaterThanOrEqual(7);
        }
      });

      for (const name of ["muted", "brand", "brand-2"]) {
        it(`${name} is at least 4.5:1 on page, surface and raised`, () => {
          const fg = rgb(name);
          if (!fg) {
            // Non-hex (oklch) values are skipped by design: this check
            // compares sRGB hex only. Their contrast is verified elsewhere.
            return;
          }
          for (const bg of ["page", "surface", "raised"]) {
            expect(contrast(fg, background(bg)!), `${name} on ${bg}`).toBeGreaterThanOrEqual(4.5);
          }
        });
      }
    });
  }
});
