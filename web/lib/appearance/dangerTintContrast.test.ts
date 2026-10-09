import { readdirSync, readFileSync, statSync } from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

import { describe, expect, it } from "vitest";

import { THEMES } from "./themes";
import { contrast, effectiveTokens, hexToRgb, over } from "./themeStylesheet";

// docs/design/brand-guidelines.md's danger-tint decision: on a danger-tinted
// background, text is fg, not danger itself — the signal comes from the
// tint and a ring, not the text color. This checks that decision stays
// true as tints change: it finds every bg-danger/<alpha> actually used in
// web source, then asserts fg stays >= 4.5:1 on that tint in every theme.
// A future tint (a new alpha, not just a new call site) is caught
// automatically, with no change needed here.
//
// Blended in sRGB (via themeStylesheet.ts's `over`), not the oklab Tailwind
// itself blends in for color-mix — a close approximation. Confirm a
// borderline result in DevTools rather than trusting this to the last
// decimal.

const WEB_ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "../..");
const SCAN_DIRS = ["app", "components", "lib", "hooks"];
const SOURCE_EXTENSIONS = [".ts", ".tsx"];

// Bare or behind any variant prefix (hover:, md:, group-hover:, ...).
const BG_DANGER_ALPHA = /(?:[\w-]+:)*bg-danger\/(\d+)/g;

function sourceFiles(dir: string): string[] {
  let entries: string[];
  try {
    entries = readdirSync(dir);
  } catch {
    return [];
  }
  return entries.flatMap((entry) => {
    if (entry === "node_modules" || entry === ".next") return [];
    const full = path.join(dir, entry);
    if (statSync(full).isDirectory()) return sourceFiles(full);
    const isSource = SOURCE_EXTENSIONS.some((ext) => entry.endsWith(ext));
    return isSource && !entry.includes(".test.") ? [full] : [];
  });
}

// Every distinct bg-danger/<alpha> percentage actually used, e.g. [10, 15, 25].
function usedAlphas(): number[] {
  const alphas = new Set<number>();
  for (const dir of SCAN_DIRS) {
    for (const file of sourceFiles(path.join(WEB_ROOT, dir))) {
      const source = readFileSync(file, "utf8");
      for (const match of source.matchAll(BG_DANGER_ALPHA)) {
        alphas.add(Number(match[1]));
      }
    }
  }
  return [...alphas].sort((a, b) => a - b);
}

const alphas = usedAlphas();
const surfaces = ["page", "surface"] as const;

for (const theme of THEMES) {
  describe(`${theme.id} danger tints`, () => {
    const tokens = effectiveTokens(theme.id);
    const fg = hexToRgb(tokens.fg ?? "");
    const danger = hexToRgb(tokens.danger ?? "");
    const hex = fg !== null && danger !== null;

    for (const alpha of alphas) {
      for (const base of surfaces) {
        it.skipIf(!hex)(`fg on danger/${alpha} over ${base} is hex (oklch is skipped)`, () => {
          const baseRgb = hexToRgb(tokens[base] ?? "")!;
          const tint = over(danger!, baseRgb, alpha / 100);
          const ratio = contrast(fg!, tint);
          expect(
            ratio,
            `${theme.id}: fg on danger/${alpha} over ${base} is ${ratio.toFixed(2)}:1`
          ).toBeGreaterThanOrEqual(4.5);
        });
      }
    }
  });
}
