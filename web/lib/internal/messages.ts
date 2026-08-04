import { and, desc, eq, isNull, lt, sql } from "drizzle-orm";

import { db } from "@/db/client";
import { dmConversations, messages, users } from "@/db/schema";

import { resolveOrCreateDmConversationId } from "./dmConversations";
import { fromChannelWireId, fromUserWireId, toChannelWireId, toUserWireId } from "./wireIds";

export class InvalidReferenceError extends Error {}

export interface WireMessage {
  message_id: number;
  channel_id: string | null;
  timestamp: number;
  user_id: string;
  username: string;
  content: string;
}

export interface CreateMessageResult {
  channel_id: string | null;
  message_id: number;
  created_at: string;
}

export interface HistoryPage {
  messages: WireMessage[];
  has_more: boolean;
}

export interface LastSequence {
  lobby_seq: number | null;
  channel_seq: number | null;
  dm_seq: number | null;
}

function toEpochSeconds(date: Date): number {
  return Math.floor(date.getTime() / 1000);
}

function orderedPair(a: string, b: string): [string, string] {
  return a < b ? [a, b] : [b, a];
}

// docs/social/friends-dms-design.md §3.4/§3.6: read-only lookup, unlike
// resolveOrCreateDmConversationId — a history fetch must never create a
// conversation row as a side effect. Returns null when no conversation
// exists yet between the pair, which callers treat as "empty history,"
// not an error (same reasoning as listInvites' "empty is a valid state").
async function findDmConversationId(rawUserIdA: string, rawUserIdB: string): Promise<string | null> {
  const [a, b] = orderedPair(rawUserIdA, rawUserIdB);
  const [row] = await db
    .select({ id: dmConversations.id })
    .from(dmConversations)
    .where(and(eq(dmConversations.userIdA, a), eq(dmConversations.userIdB, b)));
  return row?.id ?? null;
}

// docs/guilds/social-presence-design.md §4.5 / docs/social/friends-dms-design.md
// §3.4: the persistence write-through call for CHAT_MESSAGE/
// CHANNEL_MESSAGE/DM_SEND — fire-and-forget from the C++ side. channelWireId
// and peerWireId are mutually exclusive (both null = lobby); for the DM
// case this is where the conversation row actually gets created if this
// is the pair's first message (§3.4's "resolving/creating the row is an
// implementation detail of persisting the message").
export async function createMessage(
  channelWireId: string | null,
  peerWireId: string | null,
  userWireId: string,
  content: string,
  seq: number
): Promise<CreateMessageResult> {
  const userId = fromUserWireId(userWireId);
  if (!userId) {
    throw new InvalidReferenceError(`invalid user_id: ${userWireId}`);
  }

  let channelId: string | null = null;
  if (channelWireId !== null) {
    channelId = fromChannelWireId(channelWireId);
    if (!channelId) {
      throw new InvalidReferenceError(`invalid channel_id: ${channelWireId}`);
    }
  }

  let dmConversationId: string | null = null;
  if (peerWireId !== null) {
    const peerId = fromUserWireId(peerWireId);
    if (!peerId) {
      throw new InvalidReferenceError(`invalid peer_id: ${peerWireId}`);
    }
    dmConversationId = await resolveOrCreateDmConversationId(userId, peerId);
  }

  const [row] = await db.insert(messages).values({ channelId, dmConversationId, userId, content, seq }).returning();

  return {
    channel_id: channelWireId,
    message_id: row.seq,
    created_at: row.createdAt.toISOString()
  };
}

// §4.4 / §3.5: the first read-through (not write-through) internal API
// call — nothing in C++ caches this, every FETCH_HISTORY is a live round
// trip. Keyset pagination via beforeSeq, not OFFSET, per §4.4's reasoning.
// channelWireId/peerWireId mutually exclusive, both null = lobby.
// callerWireId is only used to resolve the DM scope (which conversation);
// unused for lobby/channel.
export async function getMessages(
  channelWireId: string | null,
  peerWireId: string | null,
  callerWireId: string | null,
  beforeSeq: number | null,
  limit: number
): Promise<HistoryPage> {
  let channelId: string | null = null;
  if (channelWireId !== null) {
    channelId = fromChannelWireId(channelWireId);
    if (!channelId) {
      throw new InvalidReferenceError(`invalid channel_id: ${channelWireId}`);
    }
  }

  let scopeCondition;
  if (channelWireId !== null) {
    scopeCondition = eq(messages.channelId, channelId as string);
  } else if (peerWireId !== null) {
    const callerId = callerWireId ? fromUserWireId(callerWireId) : null;
    const peerId = fromUserWireId(peerWireId);
    if (!callerId || !peerId) {
      throw new InvalidReferenceError(`invalid caller_id or peer_id: ${callerWireId} / ${peerWireId}`);
    }
    const dmConversationId = await findDmConversationId(callerId, peerId);
    if (!dmConversationId) {
      return { messages: [], has_more: false };
    }
    scopeCondition = eq(messages.dmConversationId, dmConversationId);
  } else {
    scopeCondition = and(isNull(messages.channelId), isNull(messages.dmConversationId));
  }

  const condition = beforeSeq !== null ? and(scopeCondition, lt(messages.seq, beforeSeq)) : scopeCondition;

  // Fetch one extra row to learn has_more without a second COUNT query.
  const rows = await db
    .select({
      seq: messages.seq,
      channelId: messages.channelId,
      createdAt: messages.createdAt,
      userId: messages.userId,
      content: messages.content,
      username: users.discordUsername
    })
    .from(messages)
    .innerJoin(users, eq(messages.userId, users.id))
    .where(condition)
    .orderBy(desc(messages.seq))
    .limit(limit + 1);

  const hasMore = rows.length > limit;
  // DESC for the keyset scan, reversed back to chronological order for the
  // page actually returned.
  const page = rows.slice(0, limit).reverse();

  return {
    messages: page.map((row) => ({
      message_id: row.seq,
      channel_id: row.channelId ? toChannelWireId(row.channelId) : null,
      timestamp: toEpochSeconds(row.createdAt),
      user_id: toUserWireId(row.userId),
      username: row.username,
      content: row.content
    })),
    has_more: hasMore
  };
}

// §4.3 / §3.4: fetched once at C++ server startup (lobby/channel) and
// additionally at DMHandler's own startup hydration point (dm) to seed
// each counter from the durable high-water mark instead of always
// starting at 0 — three separate id-spaces, three separate counters.
export async function getLastSequence(): Promise<LastSequence> {
  // MAX() on a bigint column comes back from the pg driver as a string, not
  // a number — node-postgres defaults bigint results to strings to avoid
  // silent precision loss for values outside JS's safe-integer range.
  // Drizzle's bigint/"number" column mode normally handles this coercion
  // for a plain column SELECT, but a raw MAX(...) aggregate bypasses that:
  // sql<number | null> is only a TypeScript type assertion, not a runtime
  // conversion, so this needs an explicit Number() or the caller silently
  // gets "5" instead of 5.
  const [lobbyRow] = await db
    .select({ maxSeq: sql<string | null>`MAX(${messages.seq})` })
    .from(messages)
    .where(and(isNull(messages.channelId), isNull(messages.dmConversationId)));
  const [channelRow] = await db
    .select({ maxSeq: sql<string | null>`MAX(${messages.seq})` })
    .from(messages)
    .where(sql`${messages.channelId} IS NOT NULL`);
  const [dmRow] = await db
    .select({ maxSeq: sql<string | null>`MAX(${messages.seq})` })
    .from(messages)
    .where(sql`${messages.dmConversationId} IS NOT NULL`);

  const toNumber = (v: string | null | undefined): number | null => (v !== null && v !== undefined ? Number(v) : null);

  return {
    lobby_seq: toNumber(lobbyRow?.maxSeq),
    channel_seq: toNumber(channelRow?.maxSeq),
    dm_seq: toNumber(dmRow?.maxSeq)
  };
}
