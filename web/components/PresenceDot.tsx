interface PresenceDotProps {
  online: boolean;
  className?: string;
}

// docs/guilds/social-presence-design.md §3: online is a fixed semantic
// green (matching SignalIndicator's connection dot — never the swappable
// theme accent, docs/settings/appearance-design.md §2), so presence reads
// the same way regardless of which theme is active.
export function PresenceDot({ online, className = "" }: PresenceDotProps) {
  return (
    <span
      className={`inline-block h-2 w-2 shrink-0 rounded-full ${online ? "bg-green-400" : "bg-slate/50"} ${className}`}
      aria-label={online ? "online" : "offline"}
    />
  );
}
