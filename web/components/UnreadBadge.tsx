import { formatBadge, unreadLabel } from "@/lib/dm/unreadCounts";

interface UnreadBadgeProps {
  count: number;
  // Positioning only (e.g. "absolute -right-1 -bottom-1" on the rail icon);
  // the pill's own look stays fixed here.
  className?: string;
}

// docs/social/friends-dms-design.md §3.7 — the DM unread pill, shared by
// the conversation list and the Friends rail icon. bg-brand/text-page is
// the pair guaranteed 4.5:1 on every theme. min-w-5 with px-1.5 keeps one
// digit round and gives "99+" room. Renders nothing at 0.
export function UnreadBadge({ count, className = "" }: UnreadBadgeProps) {
  const text = formatBadge(count);
  if (!text) {
    return null;
  }
  return (
    <span
      role="img"
      aria-label={unreadLabel(count)}
      className={`inline-flex h-5 min-w-5 shrink-0 items-center justify-center rounded-full bg-brand px-1.5 font-mono text-xs leading-none font-semibold text-page tabular-nums ${className}`}
    >
      {text}
    </span>
  );
}
