import { applyTheme } from "@/lib/appearance/applyTheme";
import { DEFAULT_THEME_ID, THEMES } from "@/lib/appearance/themes";
import type { ConnectionStatus } from "@/lib/gateway";

import { Logo } from "./Logo";
import { SignalIndicator } from "./SignalIndicator";

interface HeaderProps {
  status: ConnectionStatus;
}

// docs/settings-appearance-design.md §5 step 2: a throwaway control to
// verify local persistence end-to-end (instant switch + no flash on
// reload) ahead of the real Appearance section (step 3), which replaces
// this with a proper picker and removes this component entirely.
function ThemeDebugToggle() {
  const cycle = () => {
    const current = document.documentElement.getAttribute("data-theme") ?? DEFAULT_THEME_ID;
    const index = THEMES.findIndex((theme) => theme.id === current);
    const next = THEMES[(index + 1) % THEMES.length];
    applyTheme(next.id);
  };

  return (
    <button
      onClick={cycle}
      className="rounded-md border border-slate/40 px-2 py-1 font-mono text-xs text-ivory/60 transition-colors hover:text-ivory"
    >
      Theme
    </button>
  );
}

export function Header({ status }: HeaderProps) {
  return (
    <header className="flex shrink-0 items-center justify-between border-b border-slate/20 px-6 py-4">
      <Logo withWordmark />
      <div className="flex items-center gap-3">
        <ThemeDebugToggle />
        <SignalIndicator status={status} />
      </div>
    </header>
  );
}
