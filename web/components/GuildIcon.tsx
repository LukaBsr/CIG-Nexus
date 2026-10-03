import { GUILD_ICON_FOREGROUND, guildIconBackground, hashHue } from "@/lib/guildIconColor";

interface GuildIconProps {
  guildId: string;
  name: string;
  size?: number;
  className?: string;
}

function guildInitials(name: string): string {
  const words = name.trim().split(/\s+/).filter(Boolean);
  if (words.length >= 2) {
    return (words[0][0] + words[1][0]).toUpperCase();
  }
  return (words[0] ?? "?").slice(0, 2).toUpperCase();
}

// docs/frontend-rebuild-plan.md's Deferred section: no custom guild-icon
// upload exists in the data model (that's a separate, future extension
// mirroring docs/social/friends-dms-design.md §4.2's avatar-upload
// pattern) — every guild renders this deterministic fallback instead,
// the same way Discord itself renders a server with no uploaded icon.
// Deterministic (not random) so the same guild always gets the same
// color across reloads/accounts, without needing to persist anything.
// Theme-independent by design, same exception as PresenceDot and
// SignalIndicator's status literals (docs/design/brand-guidelines.md §2.5):
// neither the background nor the initials follow the active theme.
export function GuildIcon({ guildId, name, size = 44, className = "" }: GuildIconProps) {
  const hue = hashHue(guildId);

  return (
    <div
      style={{ width: size, height: size, backgroundColor: guildIconBackground(hue), color: GUILD_ICON_FOREGROUND }}
      className={`flex shrink-0 items-center justify-center font-mono font-semibold ${className}`}
    >
      <span style={{ fontSize: size * 0.36 }}>{guildInitials(name)}</span>
    </div>
  );
}
