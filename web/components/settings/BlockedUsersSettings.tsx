import type { SettingsSectionProps } from "@/lib/settings/sections";

import { Avatar } from "../Avatar";

// docs/social/friends-dms-design.md §2.7 (LIST_BLOCKS/UNBLOCK_USER) — the
// third SETTINGS_SECTIONS entry. Unlike ProfileSettings, this has no
// independent fetch of its own: blockedUsers/onUnblock come from
// useGatewayConnection's already-loaded WebSocket state, threaded down
// through SettingsModal (see sections.ts's comment on why these two props
// are optional on SettingsSectionProps).
export function BlockedUsersSettings({ blockedUsers = [], onUnblock }: SettingsSectionProps) {
  if (blockedUsers.length === 0) {
    return <p className="font-mono text-sm text-ivory/40">You haven&apos;t blocked anyone.</p>;
  }

  return (
    <ul className="flex list-none flex-col gap-1 p-0">
      {blockedUsers.map((b) => (
        <li
          key={b.userId}
          className="flex items-center justify-between gap-2 rounded-md bg-ink/40 px-3 py-2"
        >
          <span className="flex min-w-0 flex-1 items-center gap-2">
            <Avatar url={b.avatarUrl} name={b.displayName ?? b.username} size={24} />
            <span className="min-w-0 flex-1 truncate font-mono text-sm text-ivory">
              {b.displayName ?? b.username}
            </span>
          </span>
          <button
            onClick={() => onUnblock?.(b.userId)}
            className="shrink-0 rounded-md border border-slate/40 px-3 py-1 font-mono text-xs text-ivory transition-colors hover:border-teal hover:text-teal"
          >
            Unblock
          </button>
        </li>
      ))}
    </ul>
  );
}
