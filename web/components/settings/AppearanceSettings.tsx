"use client";

import { useState } from "react";

import { applyTheme } from "@/lib/appearance/applyTheme";
import { DEFAULT_THEME_ID, THEMES } from "@/lib/appearance/themes";

// docs/settings-appearance-design.md §5 step 3. No sync toggle yet — that's
// step 4, once users.theme/users.theme_sync_enabled and the PATCH endpoint
// exist to back it. Selecting a theme here only ever does what applyTheme
// already does: local, immediate, offline-safe (§3.1).
export function AppearanceSettings() {
  const [selected, setSelected] = useState(
    () => document.documentElement.getAttribute("data-theme") ?? DEFAULT_THEME_ID
  );

  const handleSelect = (id: string) => {
    applyTheme(id);
    setSelected(id);
  };

  return (
    <div className="flex flex-col gap-3">
      <p className="font-mono text-xs font-semibold tracking-wider text-ivory/40 uppercase">Theme</p>
      <div className="flex flex-col gap-2">
        {THEMES.map((theme) => (
          <button
            key={theme.id}
            onClick={() => handleSelect(theme.id)}
            className={`flex items-center gap-3 rounded-md border px-3 py-2 text-left transition-colors ${
              theme.id === selected
                ? "border-teal bg-teal/10"
                : "border-slate/40 hover:border-slate"
            }`}
          >
            <span className="flex shrink-0 gap-1">
              <span
                className="h-4 w-4 rounded-full border border-ink/20"
                style={{ backgroundColor: theme.swatch.accent }}
              />
              <span
                className="h-4 w-4 rounded-full border border-ink/20"
                style={{ backgroundColor: theme.swatch.secondary }}
              />
            </span>
            <span className="font-mono text-sm text-ivory">{theme.label}</span>
          </button>
        ))}
      </div>
    </div>
  );
}
