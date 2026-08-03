import type { ConnectionStatus } from "@/lib/gateway";

import { Logo } from "./Logo";
import { SignalIndicator } from "./SignalIndicator";

interface HeaderProps {
  status: ConnectionStatus;
  onOpenSettings: () => void;
}

function GearIcon() {
  return (
    <svg width="16" height="16" viewBox="0 0 16 16" fill="none" stroke="currentColor" strokeWidth="1.5">
      <circle cx="8" cy="8" r="2.5" />
      <circle cx="8" cy="8" r="5.5" strokeDasharray="1.6 2.2" strokeLinecap="round" />
    </svg>
  );
}

export function Header({ status, onOpenSettings }: HeaderProps) {
  return (
    <header className="flex shrink-0 items-center justify-between border-b border-slate/20 px-6 py-4">
      <Logo withWordmark />
      <div className="flex items-center gap-4">
        <button
          onClick={onOpenSettings}
          aria-label="Open settings"
          className="text-ivory/50 transition-colors hover:text-ivory"
        >
          <GearIcon />
        </button>
        <SignalIndicator status={status} />
      </div>
    </header>
  );
}
