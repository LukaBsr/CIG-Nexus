import { and, desc, eq, isNull, lt, sql } from "drizzle-orm";

import { db } from "@/db/client";
import { messages, users } from "@/db/schema";

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
}

function toEpochSeconds(date: Date): number {
  return Math.floor(date.getTime() / 1000);
}

// docs/guilds/social-presence-design.md §4.5: the persistence write-through call
// for CHAT_MESSAGE/CHANNEL_MESSAGE — fire-and-forget from the C++ side, so
// the caller here (the C++ worker) already has and broadcast every field
// this message needs; the response only needs to confirm what was stored,
// not echo back display data (username etc.) nobody downstream is waiting
// on.
export async function createMessage(
  channelWireId: string | null,
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

  const [row] = await db.insert(messages).values({ channelId, userId, content, seq }).returning();

  return {
    channel_id: channelWireId,
    message_id: row.seq,
    created_at: row.createdAt.toISOString()
  };
}

// §4.4: the first read-through (not write-through) internal API call —
// nothing in C++ caches this, every FETCH_HISTORY is a live round trip.
// Keyset pagination via beforeSeq, not OFFSET, per §4.4's reasoning.
export async function getMessages(
  channelWireId: string | null,
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

  const scopeCondition = channelId === null ? isNull(messages.channelId) : eq(messages.channelId, channelId);
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

// §4.3: fetched once at C++ server startup to seed ChatHandler's and
// ChannelHandler's message_id counters from the durable high-water mark,
// instead of always starting at 0 — the fix for the counter otherwise
// colliding with already-persisted ids after a restart.
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
    .where(isNull(messages.channelId));
  const [channelRow] = await db
    .select({ maxSeq: sql<string | null>`MAX(${messages.seq})` })
    .from(messages)
    .where(sql`${messages.channelId} IS NOT NULL`);

  return {
    lobby_seq: lobbyRow?.maxSeq !== null && lobbyRow?.maxSeq !== undefined ? Number(lobbyRow.maxSeq) : null,
    channel_seq:
      channelRow?.maxSeq !== null && channelRow?.maxSeq !== undefined ? Number(channelRow.maxSeq) : null
  };
}
