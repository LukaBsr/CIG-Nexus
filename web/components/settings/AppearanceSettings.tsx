"use client";

import { useState } from "react";

import { patchAppearance } from "@/lib/appearance/api";
import { applyTheme } from "@/lib/appearance/applyTheme";
import { readClientCookie, THEME_SYNC_COOKIE, writeClientCookie } from "@/lib/appearance/cookie";
import { DEFAULT_THEME_ID, THEMES } from "@/lib/appearance/themes";

// docs/settings-appearance-design.md §5 step 4. Local application (§3.1)
// stays unconditional regardless of sync state — the toggle only ever
// controls whether a change *also* gets pushed to the account (§3.3).
export function AppearanceSettings() {
  const [selected, setSelected] = useState(
    () => document.documentElement.getAttribute("data-theme") ?? DEFAULT_THEME_ID
  );
  const [syncEnabled, setSyncEnabled] = useState(() => readClientCookie(THEME_SYNC_COOKIE) === "1");
  const [syncPending, setSyncPending] = useState(false);
  const [syncError, setSyncError] = useState<string | null>(null);

  const handleSelect = (id: string) => {
    applyTheme(id);
    setSelected(id);
    if (syncEnabled) {
      // Best-effort — a failed push doesn't block or revert the local
      // switch, which always stays authoritative for this device (§3.1).
      void patchAppearance({ theme: id });
    }
  };

  const handleToggleSync = async (checked: boolean) => {
    setSyncPending(true);
    setSyncError(null);

    // §3.7 (approved): turning sync on pushes this device's current theme
    // as the new account default — last-write-wins, not a merge/prompt.
    const result = await patchAppearance(
      checked ? { sync_enabled: true, theme: selected } : { sync_enabled: false }
    );

    setSyncPending(false);

    if (!result) {
      setSyncError("Couldn't update sync — try again.");
      return;
    }

    writeClientCookie(THEME_SYNC_COOKIE, result.sync_enabled ? "1" : "0");
    setSyncEnabled(result.sync_enabled);
  };

  return (
    <div className="flex flex-col gap-5">
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

      <div className="flex flex-col gap-2 border-t border-slate/20 pt-4">
        <label className="flex cursor-pointer items-center justify-between gap-3">
          <span className="font-mono text-sm text-ivory">Sync across devices</span>
          <input
            type="checkbox"
            checked={syncEnabled}
            disabled={syncPending}
            onChange={(e) => void handleToggleSync(e.target.checked)}
            className="h-4 w-4 accent-teal"
          />
        </label>
        <p className="font-mono text-xs text-ivory/40">
          {syncEnabled
            ? "This device's theme is saved to your account and applied on every device where sync is on."
            : "This theme only applies on this device."}
        </p>
        {syncError && <p className="font-mono text-xs text-red-400">{syncError}</p>}
      </div>
    </div>
  );
}
