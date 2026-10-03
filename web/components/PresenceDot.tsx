interface PresenceDotProps {
  online: boolean;
  className?: string;
}

// docs/guilds/social-presence-design.md §3: online is the `online` status
// token, not the swappable accent (docs/settings/appearance-design.md §2),
// so presence keeps its meaning across themes (docs/design/brand-guidelines.md
// §2.5). A theme may tune the shade, never the hue's meaning.
export function PresenceDot({ online, className = "" }: PresenceDotProps) {
  return (
    <span
      className={`inline-block h-2 w-2 shrink-0 rounded-full ${online ? "bg-online" : "bg-slate/50"} ${className}`}
      aria-label={online ? "online" : "offline"}
    />
  );
}
