// docs/social/friends-dms-design.md §3.7 — the web client's DM unread
// state, as pure functions over plain records so it is testable without
// React. useGatewayConnection owns the state and only wires wire messages
// to these.

// peer user_id -> unread count. An absent key means 0, the same "missing
// is a real state" shape UNREAD_COUNTS uses on the wire.
export type UnreadCounts = Record<string, number>;

// peer user_id -> highest DM message_id already accounted for from that
// peer (counted, marked read, or covered by a DM_READ marker). What makes
// counting idempotent by message_id.
export type HighestSeen = Record<string, number>;

// The server caps every count at 100 (shared/protocol/README.md,
// FETCH_UNREAD_COUNTS) and the badge shows "99+" from there.
const BADGE_CAP = 100;

export type DmMessageAction = "ignore" | "mark-read" | "increment";

// UNREAD_COUNTS replaces the whole map — the server is authoritative, so
// the previous map is deliberately discarded, never merged into. It's a
// parameter so the hook applies this as a state updater and the
// replace-not-merge contract is a tested property, not a call-site detail.
export function applyUnreadCounts(
  previous: UnreadCounts,
  counts: Record<string, number>
): UnreadCounts {
  const next: UnreadCounts = {};
  for (const [peerId, count] of Object.entries(counts)) {
    if (count > 0) {
      next[peerId] = count;
    }
  }
  return next;
}

// What a live DM_MESSAGE does to the unread state. Our own messages never
// count; a message_id not above the highest already seen from that peer
// is a duplicate; a message the user is reading right now (active
// conversation, Friends view showing, tab visible and focused) is marked
// read instead of counted.
export function classifyDmMessage(
  message: { senderId: string; messageId: number },
  myUserId: string | null,
  highestSeen: HighestSeen,
  isBeingRead: boolean
): DmMessageAction {
  if (message.senderId === myUserId) {
    return "ignore";
  }
  if (message.messageId <= (highestSeen[message.senderId] ?? 0)) {
    return "ignore";
  }
  return isBeingRead ? "mark-read" : "increment";
}

export function noteSeen(highestSeen: HighestSeen, peerId: string, messageId: number): HighestSeen {
  if (messageId <= (highestSeen[peerId] ?? 0)) {
    return highestSeen;
  }
  return { ...highestSeen, [peerId]: messageId };
}

export function incrementUnread(counts: UnreadCounts, peerId: string): UnreadCounts {
  return { ...counts, [peerId]: (counts[peerId] ?? 0) + 1 };
}

// DM_READ carries the reader's remaining unread count for that peer — set
// it outright. This is what clears a badge on the reader's other tabs.
export function applyDmRead(counts: UnreadCounts, peerId: string, unread: number): UnreadCounts {
  const next = { ...counts };
  if (unread > 0) {
    next[peerId] = unread;
  } else {
    delete next[peerId];
  }
  return next;
}

export function totalUnread(counts: UnreadCounts): number {
  return Object.values(counts).reduce((sum, count) => sum + count, 0);
}

export function formatBadge(count: number): string {
  if (count <= 0) {
    return "";
  }
  return count >= BADGE_CAP ? "99+" : String(count);
}

export function unreadLabel(count: number): string {
  if (count >= BADGE_CAP) {
    return "More than 99 unread messages";
  }
  return count === 1 ? "1 unread message" : `${count} unread messages`;
}
