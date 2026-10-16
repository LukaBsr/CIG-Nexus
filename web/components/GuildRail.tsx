import { unreadLabel } from "@/lib/dm/unreadCounts";
import type { Guild } from "@/lib/types";

import { GuildBrowsePopover } from "./GuildBrowsePopover";
import { GuildIcon } from "./GuildIcon";
import { Mark } from "./Mark";
import { UnreadBadge } from "./UnreadBadge";

export type RailView = "lobby" | "guild" | "friends";

interface GuildRailProps {
  guilds: Guild[];
  myGuildIds: Set<string>;
  myPendingJoinRequestGuildIds: Set<string>;
  activeView: RailView;
  activeGuildId: string | null;
  onSelectLobby: () => void;
  onSelectGuild: (guildId: string) => void;
  onSelectFriends: () => void;
  // Sum of every DM conversation's unread count (§3.7).
  unreadDmTotal: number;
  onJoinGuild: (guildId: string) => void;
  onRequestJoin: (guildId: string) => void;
  onCreateGuild: (name: string) => void;
}

// The left-edge pill every active rail item gets (Lobby, a guild, or
// Friends) — factored out so all three use the exact same indicator
// rather than three near-identical inline spans.
function RailActiveIndicator() {
  return <span className="absolute -left-[10px] h-6 w-1 rounded-r-full bg-fg" aria-hidden="true" />;
}

function PeopleIcon() {
  return (
    <svg width="20" height="20" viewBox="0 0 20 20" fill="none" stroke="currentColor" strokeWidth="1.6" strokeLinecap="round" strokeLinejoin="round">
      <circle cx="7.5" cy="7" r="2.75" />
      <path d="M2 16c0-2.8 2.2-4.5 5.5-4.5S13 13.2 13 16" />
      <path d="M13.5 8a2.25 2.25 0 1 0 0-4.5" />
      <path d="M15.5 11.5c2 .3 3 1.5 3 4.5" />
    </svg>
  );
}

// docs/frontend-rebuild-plan.md's Deferred section — replaces the
// horizontal Lobby/Guilds/Friends TabGroup with a persistent Discord-
// style vertical icon rail: a pinned Lobby icon, one icon per guild the
// user belongs to (existing LIST_GUILDS order — no drag-to-reorder),
// then a pinned Friends icon. The Friends icon carries one badge: the
// total of unread DMs (docs/social/friends-dms-design.md §3.7). There are
// no guild unread/mention badges, and the incoming-friend-request count
// (FriendRequestsPopover) still shows only inside the Friends view, not
// duplicated onto this icon.
export function GuildRail({
  guilds,
  myGuildIds,
  myPendingJoinRequestGuildIds,
  activeView,
  activeGuildId,
  onSelectLobby,
  onSelectGuild,
  onSelectFriends,
  unreadDmTotal,
  onJoinGuild,
  onRequestJoin,
  onCreateGuild
}: GuildRailProps) {
  const memberGuilds = guilds.filter((g) => myGuildIds.has(g.guildId));

  return (
    <nav className="flex w-[72px] shrink-0 flex-col items-center gap-2 border-r border-slate/20 bg-surface/40 py-3">
      <button
        onClick={onSelectLobby}
        aria-label="Lobby"
        title="Lobby"
        className={`relative flex h-11 w-11 shrink-0 items-center justify-center bg-surface transition-all ${
          activeView === "lobby" ? "rounded-xl" : "rounded-full hover:rounded-xl"
        }`}
      >
        {activeView === "lobby" && <RailActiveIndicator />}
        <Mark size={26} className="shrink-0" />
      </button>

      <div className="h-px w-8 shrink-0 bg-slate/30" />

      <div className="flex min-h-0 w-full flex-1 flex-col items-center gap-2 overflow-y-auto">
        {memberGuilds.map((g) => {
          const isActive = activeView === "guild" && g.guildId === activeGuildId;
          return (
            <button
              key={g.guildId}
              onClick={() => onSelectGuild(g.guildId)}
              aria-label={g.name}
              title={g.name}
              className="relative flex h-11 w-11 shrink-0 items-center justify-center"
            >
              {isActive && <RailActiveIndicator />}
              <GuildIcon
                guildId={g.guildId}
                name={g.name}
                size={44}
                className={`transition-all ${isActive ? "rounded-xl" : "rounded-full hover:rounded-xl"}`}
              />
            </button>
          );
        })}
      </div>

      <GuildBrowsePopover
        guilds={guilds}
        myGuildIds={myGuildIds}
        myPendingJoinRequestGuildIds={myPendingJoinRequestGuildIds}
        onJoinGuild={onJoinGuild}
        onRequestJoin={onRequestJoin}
        onCreateGuild={onCreateGuild}
      />

      <div className="h-px w-8 shrink-0 bg-slate/30" />

      <button
        onClick={onSelectFriends}
        aria-label={unreadDmTotal > 0 ? `Friends, ${unreadLabel(unreadDmTotal)}` : "Friends"}
        title="Friends"
        className={`relative flex h-11 w-11 shrink-0 items-center justify-center transition-all ${
          activeView === "friends" ? "rounded-xl bg-brand/20 text-brand" : "rounded-full bg-surface text-muted hover:rounded-xl hover:text-fg"
        }`}
      >
        {activeView === "friends" && <RailActiveIndicator />}
        <PeopleIcon />
        <UnreadBadge count={unreadDmTotal} className="absolute -top-1 -right-1.5 ring-2 ring-page" />
      </button>
    </nav>
  );
}
