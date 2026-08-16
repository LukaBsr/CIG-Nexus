interface GuildIconProps {
  guildId: string;
  name: string;
  size?: number;
  className?: string;
}

// docs/frontend-rebuild-plan.md's Deferred section: no custom guild-icon
// upload exists in the data model (that's a separate, future extension
// mirroring docs/social/friends-dms-design.md §4.2's avatar-upload
// pattern) — every guild renders this deterministic fallback instead,
// the same way Discord itself renders a server with no uploaded icon.
// Deterministic (not random) so the same guild always gets the same
// color across reloads/accounts, without needing to persist anything.
function hashHue(id: string): number {
  let hash = 0;
  for (let i = 0; i < id.length; i++) {
    hash = (hash * 31 + id.charCodeAt(i)) | 0;
  }
  return Math.abs(hash) % 360;
}

function guildInitials(name: string): string {
  const words = name.trim().split(/\s+/).filter(Boolean);
  if (words.length >= 2) {
    return (words[0][0] + words[1][0]).toUpperCase();
  }
  return (words[0] ?? "?").slice(0, 2).toUpperCase();
}

export function GuildIcon({ guildId, name, size = 44, className = "" }: GuildIconProps) {
  const hue = hashHue(guildId);

  return (
    <div
      style={{ width: size, height: size, backgroundColor: `hsl(${hue}, 45%, 32%)` }}
      className={`flex shrink-0 items-center justify-center font-mono font-semibold text-ivory ${className}`}
    >
      <span style={{ fontSize: size * 0.36 }}>{guildInitials(name)}</span>
    </div>
  );
}
