import { and, eq, sql } from "drizzle-orm";

import { db } from "@/db/client";
import { dmConversations, dmReadState, messages } from "@/db/schema";

import { fromUserWireId, toUserWireId } from "./wireIds";

// docs/social/friends-dms-design.md §3.7. Server-enforced cap: the client
// shows "99+" once a count reaches this; the server never reports more,
// and every count query below is itself bounded to CAP + 1 rows scanned
// (via LIMIT on an index range scan, idx_messages_dm_created), so a
// conversation with thousands of unread messages costs the same as one
// with a hundred.
const UNREAD_CAP = 100;

function orderedPair(a: string, b: string): [string, string] {
  return a < b ? [a, b] : [b, a];
}

// Read-only counterpart to dmConversations.ts's resolveOrCreateDmConversationId
// — marking a conversation read must never create one. A peer with no
// conversation at all (never exchanged a message) has nothing to mark
// read, which is exactly the "invalid" markDmRead already returns for an
// up_to_seq outside the conversation — the same check, one query earlier.
async function findExistingDmConversationId(rawUserIdA: string, rawUserIdB: string): Promise<string | null> {
  const [a, b] = orderedPair(rawUserIdA, rawUserIdB);
  const [existing] = await db
    .select({ id: dmConversations.id })
    .from(dmConversations)
    .where(and(eq(dmConversations.userIdA, a), eq(dmConversations.userIdB, b)));
  return existing?.id ?? null;
}

async function countUnread(dmConversationId: string, markerSeq: number, readerId: string): Promise<number> {
  const result = await db.execute<{ count: number }>(sql`
    SELECT count(*)::int AS count FROM (
      SELECT 1 FROM ${messages}
      WHERE ${messages.dmConversationId} = ${dmConversationId}
        AND ${messages.seq} > ${markerSeq}
        AND ${messages.userId} <> ${readerId}
      ORDER BY ${messages.seq}
      LIMIT ${UNREAD_CAP + 1}
    ) bounded
  `);
  return Math.min(result.rows[0].count, UNREAD_CAP);
}

export type MarkDmReadResult =
  | { ok: true; markerSeq: number; unread: number }
  | { ok: false; reason: "invalid" };

// §3.7 (MARK_DM_READ). Monotonic: a lower up_to_seq than the current
// marker never moves it backward (GREATEST in the upsert), but still
// succeeds — idempotent, not an error, since a retried or out-of-order
// client call is the ordinary case this matters for.
//
// Does NOT verify up_to_seq against the messages table — the caller
// (DMReadStateHandler, C++) already bounds it against the server's own
// in-memory DM sequence counter before ever calling this. A Postgres
// check here would race DM_SEND's fire-and-forget persistence
// (§3.4/§4.5's Option B): a client can legitimately send MARK_DM_READ for
// a message_id it just received over the wire before that message's row
// exists in this table, and rejecting it then would be wrong, not safe.
// The only check left here is that a conversation exists at all between
// these two users — a peer with no conversation ever (not even one still
// in the persistence queue pointed at a conversation id that also
// doesn't exist yet) has nothing to mark read.
export async function markDmRead(
  callerWireId: string,
  peerWireId: string,
  upToSeq: number
): Promise<MarkDmReadResult> {
  const callerId = fromUserWireId(callerWireId);
  const peerId = fromUserWireId(peerWireId);
  if (!callerId || !peerId) {
    return { ok: false, reason: "invalid" };
  }

  const conversationId = await findExistingDmConversationId(callerId, peerId);
  if (!conversationId) {
    return { ok: false, reason: "invalid" };
  }

  const [row] = await db
    .insert(dmReadState)
    .values({ userId: callerId, dmConversationId: conversationId, lastReadSeq: upToSeq })
    .onConflictDoUpdate({
      target: [dmReadState.userId, dmReadState.dmConversationId],
      set: {
        lastReadSeq: sql`greatest(${dmReadState.lastReadSeq}, ${upToSeq})`,
        updatedAt: new Date()
      }
    })
    .returning({ lastReadSeq: dmReadState.lastReadSeq });

  const unread = await countUnread(conversationId, row.lastReadSeq, callerId);
  return { ok: true, markerSeq: row.lastReadSeq, unread };
}

export interface WireUnreadCount {
  peer_id: string;
  count: number;
}

// §3.7 (FETCH_UNREAD_COUNTS). One SQL statement for every one of the
// caller's DM conversations, not one call per conversation — the cost
// constraint this was specifically asked to meet. Starts from
// dm_conversations (every conversation the caller participates in), not
// dm_read_state: a brand-new conversation has no read-state row at all
// until the first MARK_DM_READ, and starting from dm_read_state would
// make that conversation's first messages invisible here, never counted
// as unread. The LEFT JOIN + COALESCE(..., 0) treats a missing row as
// "nothing read yet," the same meaning migration 0009's backfill gives
// it. Only conversations with a count above 0 are returned; the client
// already treats an absent peer as "no unread."
export async function fetchUnreadCounts(userWireId: string): Promise<WireUnreadCount[] | null> {
  const userId = fromUserWireId(userWireId);
  if (!userId) {
    return null;
  }

  const result = await db.execute<{ peer_id: string; count: number }>(sql`
    SELECT
      CASE WHEN dc.user_id_a = ${userId} THEN dc.user_id_b ELSE dc.user_id_a END AS peer_id,
      LEAST(c.cnt, ${UNREAD_CAP})::int AS count
    FROM ${dmConversations} dc
    LEFT JOIN ${dmReadState} drs ON drs.dm_conversation_id = dc.id AND drs.user_id = ${userId}
    CROSS JOIN LATERAL (
      SELECT count(*)::int AS cnt FROM (
        SELECT 1 FROM ${messages} m
        WHERE m.dm_conversation_id = dc.id
          AND m.seq > COALESCE(drs.last_read_seq, 0)
          AND m.user_id <> ${userId}
        ORDER BY m.seq
        LIMIT ${UNREAD_CAP + 1}
      ) bounded
    ) c
    WHERE (dc.user_id_a = ${userId} OR dc.user_id_b = ${userId}) AND c.cnt > 0
  `);

  return result.rows.map((row) => ({ peer_id: toUserWireId(row.peer_id), count: row.count }));
}
