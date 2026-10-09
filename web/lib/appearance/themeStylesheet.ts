import { readFileSync } from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

import { THEMES } from "./themes";

// Test-only reader for web/app/globals.css. Lives next to the registry so the
// drift and contrast tests share one parser; nothing in the app imports it.
// Reads the real stylesheet rather than copying values, so tests fail when
// the CSS drifts from the registry.

export const TOKEN_NAMES = [
  "page",
  "surface",
  "raised",
  "slate",
  "fg",
  "muted",
  "brand",
  "brand-2",
  "danger",
  "warning",
  "online",
] as const;

const here = path.dirname(fileURLToPath(import.meta.url));
const source = readFileSync(path.join(here, "../../app/globals.css"), "utf8").replace(
  /\/\*[\s\S]*?\*\//g,
  ""
);

// Body of the first `header { ... }` block, brace-matched.
function blockBody(src: string, header: RegExp): string | null {
  const start = src.search(header);
  if (start < 0) return null;
  const open = src.indexOf("{", start);
  let depth = 0;
  for (let i = open; i < src.length; i++) {
    if (src[i] === "{") depth++;
    else if (src[i] === "}" && --depth === 0) return src.slice(open + 1, i);
  }
  throw new Error(`unterminated block: ${header}`);
}

// Every `--color-*` and `color-scheme` declaration in a block body, keyed by
// token name without the prefix ("brand", not "--color-brand").
function declarations(body: string): Record<string, string> {
  const out: Record<string, string> = {};
  for (const m of body.matchAll(/(--color-[a-z0-9-]+|color-scheme)\s*:\s*([^;]+);/g)) {
    out[m[1].replace(/^--color-/, "")] = m[2].trim();
  }
  return out;
}

// Raw declarations per theme, as written. abyss is the @theme defaults
// (globals.css declares no [data-theme="abyss"] block by design); the rest
// are their [data-theme] blocks, which may redeclare only some tokens.
const abyssRaw = declarations(blockBody(source, /@theme\s*\{/) ?? "");
const rootRaw = declarations(blockBody(source, /:root\s*\{/) ?? "");

export function rawBlock(id: string): Record<string, string> | null {
  if (id === "abyss") return abyssRaw;
  const body = blockBody(source, new RegExp(`\\[data-theme="${id}"\\]\\s*\\{`));
  return body === null ? null : declarations(body);
}

// Every [data-theme="..."] id declared in globals.css, for orphan checks.
export function declaredThemeIds(): string[] {
  return [...source.matchAll(/\[data-theme="([a-z0-9-]+)"\]\s*\{/g)].map((m) => m[1]);
}

// What the cascade actually gives a theme: @theme defaults, overlaid by the
// theme's block. This is the only meaningful "tokens of theme X" view, since
// Ember inherits the tokens it does not redeclare.
export function effectiveTokens(id: string): Record<string, string> {
  return { ...abyssRaw, ...(rawBlock(id) ?? {}) };
}

// color-scheme as the cascade resolves it for a theme (its block, else :root).
export function effectiveColorScheme(id: string): string | undefined {
  return rawBlock(id)?.["color-scheme"] ?? rootRaw["color-scheme"];
}

export function hexToRgb(value: string): [number, number, number] | null {
  const m = /^#([0-9a-fA-F]{6})$/.exec(value);
  if (!m) return null;
  const n = parseInt(m[1], 16);
  return [(n >> 16) & 255, (n >> 8) & 255, n & 255];
}

// WCAG contrast math, shared by every test that checks a color against a
// surface (themeContrast.test.ts, dangerTintContrast.test.ts). sRGB, not the
// oklab Tailwind's color-mix actually blends in — a close approximation;
// confirm a borderline case in DevTools rather than trusting this to the
// last decimal.
export type RGB = [number, number, number];

export function luminance([r, g, b]: RGB): number {
  const lin = (v: number) => {
    const s = v / 255;
    return s <= 0.03928 ? s / 12.92 : ((s + 0.055) / 1.055) ** 2.4;
  };
  return 0.2126 * lin(r) + 0.7152 * lin(g) + 0.0722 * lin(b);
}

export function contrast(a: RGB, b: RGB): number {
  const la = luminance(a);
  const lb = luminance(b);
  return (Math.max(la, lb) + 0.05) / (Math.min(la, lb) + 0.05);
}

// `fg` composited over `bg` at `alpha` (0-1), e.g. a danger tint's bg-danger/15.
export function over(fg: RGB, bg: RGB, alpha: number): RGB {
  return [0, 1, 2].map((i) => fg[i] * alpha + bg[i] * (1 - alpha)) as RGB;
}

export function registeredIds(): string[] {
  return THEMES.map((theme) => theme.id);
}

// Reads a source file under web/ (for the component-constant drift check).
export function readWebFile(relative: string): string {
  return readFileSync(path.join(here, "../..", relative), "utf8");
}
