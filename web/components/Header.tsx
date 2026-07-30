import type { ConnectionStatus } from "@/lib/gateway";

import { Logo } from "./Logo";
import { SignalIndicator } from "./SignalIndicator";

interface HeaderProps {
  status: ConnectionStatus;
}

export function Header({ status }: HeaderProps) {
  return (
    <header className="flex shrink-0 items-center justify-between border-b border-slate/20 px-6 py-4">
      <Logo withWordmark />
      <SignalIndicator status={status} />
    </header>
  );
}
