// docs/settings/appearance-design.md §2.2: the registry is content, the
// data-theme attribute + CSS custom properties (web/app/globals.css) are
// the mechanism — adding a theme means one CSS block plus one entry here,
// never a change to any consuming component's bg-*/text-*/border-* classes.
// The drift guard in themes.drift.test.ts fails if an entry and its block
// disagree (missing block, missing token, color-scheme, or swatch).

export type ThemeMode = "dark" | "light";

export interface ThemeDefinition {
  id: string;
  label: string;
  // Drives the picker's grouping (AppearanceSettings) and the block's
  // color-scheme. Only "dark" themes ship today.
  mode: ThemeMode;
  // Preview swatch for the Appearance picker (AppearanceSettings renders it).
  // Hex values are data, not class names, so they are not tied to the token
  // names; they must equal the block's --color-brand (accent) and
  // --color-brand-2 (secondary) in globals.css.
  swatch: { accent: string; secondary: string };
}

export const THEMES: ThemeDefinition[] = [
  { id: "abyss", label: "Abyss (default)", mode: "dark", swatch: { accent: "#5eead4", secondary: "#a079f8" } },
  { id: "ember", label: "Ember", mode: "dark", swatch: { accent: "#f87171", secondary: "#fb923c" } },
  { id: "onyx", label: "Onyx", mode: "dark", swatch: { accent: "#6f7af4", secondary: "#eb459e" } },
  { id: "mocha", label: "Mocha", mode: "dark", swatch: { accent: "#cba6f7", secondary: "#f5c2e7" } },
  { id: "amethyst", label: "Amethyst", mode: "dark", swatch: { accent: "#b794ff", secondary: "#f472b6" } },
  { id: "espresso", label: "Espresso", mode: "dark", swatch: { accent: "#c3b090", secondary: "#db94a3" } }
];

export const DEFAULT_THEME_ID = "abyss";

// §2.3: an unrecognized id (a removed theme, a corrupted cookie/DB value)
// degrades to the default rather than erroring or rendering unstyled.
export function resolveThemeId(id: string | null | undefined): string {
  if (id && THEMES.some((theme) => theme.id === id)) {
    return id;
  }
  return DEFAULT_THEME_ID;
}
