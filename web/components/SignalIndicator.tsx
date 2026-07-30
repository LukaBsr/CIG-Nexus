import type { ConnectionStatus } from "@/lib/gateway";

interface StatusConfig {
  label: string;
  dot: string;
  ring: string;
  pulse: boolean;
}

const STATUS_CONFIG: Record<ConnectionStatus, StatusConfig> = {
  connected: { label: "connected", dot: "bg-teal", ring: "ring-teal/30", pulse: true },
  connecting: { label: "connecting…", dot: "bg-teal/50", ring: "ring-teal/10", pulse: true },
  disconnected: { label: "disconnected", dot: "bg-slate", ring: "ring-slate/20", pulse: false },
  unauthenticated: { label: "signed out", dot: "bg-slate", ring: "ring-slate/20", pulse: false },
  error: { label: "connection error", dot: "bg-red-400", ring: "ring-red-400/20", pulse: false }
};

interface SignalIndicatorProps {
  status: ConnectionStatus;
}

// The signature element: a live ring+dot, echoing the crystal mark's own
// terminal glyph (web/public/branding/icon.svg — a ringed dot at the base
// of the shard, where the connection "arrives"). Driven entirely by the
// existing ConnectionStatus from useGatewayConnection; no new state.
export function SignalIndicator({ status }: SignalIndicatorProps) {
  const config = STATUS_CONFIG[status];

  return (
    <div className="flex items-center gap-2 font-mono text-xs text-ivory/70">
      <span className="relative flex h-2.5 w-2.5">
        {config.pulse && (
          <span
            className={`absolute inline-flex h-full w-full animate-ping rounded-full opacity-75 ${config.dot}`}
          />
        )}
        <span className={`relative inline-flex h-2.5 w-2.5 rounded-full ring-4 ${config.dot} ${config.ring}`} />
      </span>
      <span>{config.label}</span>
    </div>
  );
}
