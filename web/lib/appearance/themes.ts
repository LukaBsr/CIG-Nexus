// docs/settings/appearance-design.md §2.2: the registry is content, the
// data-theme attribute + CSS custom properties (web/app/globals.css) are
// the mechanism — adding a theme means one CSS block plus one entry here,
// never a change to any consuming component's bg-*/text-*/border-* classes.

export interface ThemeDefinition {
  id: string;
  label: string;
  // Preview swatch for the Appearance picker (AppearanceSettings renders it).
  // Hex values are data, not class names, so they are not tied to the token
  // names; keep them in sync with globals.css's --color-brand/--color-brand-2.
  swatch: { accent: string; secondary: string };
}

export const THEMES: ThemeDefinition[] = [
  { id: "abyss", label: "Abyss (default)", swatch: { accent: "#5eead4", secondary: "#a079f8" } },
  { id: "ember", label: "Ember", swatch: { accent: "#f87171", secondary: "#fb923c" } }
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
