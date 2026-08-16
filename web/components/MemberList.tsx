import { OFFICER_RANK, OWNER_RANK } from "@/lib/roles";
import type { Member } from "@/lib/types";

import { Avatar } from "./Avatar";
import { PresenceDot } from "./PresenceDot";

interface MemberListProps {
  members: Member[];
  onlineUserIds: Set<string>;
  myUserId: string | null;
  onBlock: (userId: string) => void;
}

function roleLabelColor(roleRank: number): string {
  if (roleRank >= OWNER_RANK) return "text-violet";
  if (roleRank >= OFFICER_RANK) return "text-teal";
  return "text-ivory/40";
}

// The roster panel — data has existed since docs/guilds/social-presence-design.md
// (fetched for permission gating, MEMBER_LIST/MEMBER_ROLE_UPDATED) but never
// had a visible place to live until this pass. Online-first, then rank,
// then name, matching the conventional "who's actually here" ordering.
export function MemberList({ members, onlineUserIds, myUserId, onBlock }: MemberListProps) {
  const sorted = [...members].sort((a, b) => {
    const aOnline = onlineUserIds.has(a.userId);
    const bOnline = onlineUserIds.has(b.userId);
    if (aOnline !== bOnline) return aOnline ? -1 : 1;
    if (b.roleRank !== a.roleRank) return b.roleRank - a.roleRank;
    return a.username.localeCompare(b.username);
  });

  return (
    <aside className="flex w-52 shrink-0 flex-col overflow-y-auto border-l border-slate/20 p-3">
      <h2 className="mb-2 px-1 font-mono text-xs font-semibold tracking-wider text-ivory/40 uppercase">
        Members — {members.length}
      </h2>
      <ul className="flex list-none flex-col gap-0.5 p-0">
        {sorted.map((m) => (
          <li
            key={m.userId}
            className="flex items-center gap-2 rounded-md px-1.5 py-1 transition-colors hover:bg-surface/60"
          >
            <PresenceDot online={onlineUserIds.has(m.userId)} />
            <Avatar url={m.avatarUrl} name={m.displayName ?? m.username} size={20} />
            <span className="min-w-0 flex-1 truncate font-mono text-sm text-ivory/90">
              {m.displayName ?? m.username}
            </span>
            <span className={`shrink-0 font-mono text-[10px] uppercase ${roleLabelColor(m.roleRank)}`}>
              {m.roleLabel}
            </span>
            {m.userId !== myUserId && (
              <button
                onClick={() => onBlock(m.userId)}
                aria-label={`Block ${m.displayName ?? m.username}`}
                title="Block"
                className="shrink-0 font-mono text-xs text-ivory/25 transition-colors hover:text-red-400"
              >
                &times;
              </button>
            )}
          </li>
        ))}
      </ul>
    </aside>
  );
}
