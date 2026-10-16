import { sql } from "drizzle-orm";
import { afterEach, describe, expect, it } from "vitest";

import { db } from "@/db/client";
import { dmConversations, dmReadState, messages, users } from "@/db/schema";

import { resolveOrCreateDmConversationId } from "./dmConversations";
import { fetchUnreadCounts, markDmRead } from "./dmReadState";
import { toUserWireId } from "./wireIds";

afterEach(async () => {
  await db.execute(
    sql`TRUNCATE TABLE ${dmReadState}, ${messages}, ${dmConversations}, ${users} RESTART IDENTITY CASCADE`
  );
});

async function insertUser(discordId: string) {
  const [user] = await db
    .insert(users)
    .values({ discordId, discordUsername: `user_${discordId}` })
    .returning();
  return user;
}

let nextSeq = 1;
async function sendMessage(dmConversationId: string, userId: string): Promise<number> {
  const seq = nextSeq++;
  await db.insert(messages).values({ dmConversationId, userId, content: "hi", seq });
  return seq;
}

describe("markDmRead", () => {
  it("rejects a malformed caller or peer id", async () => {
    const a = await insertUser("1");
    expect(await markDmRead("not-a-wire-id", toUserWireId(a.id), 1)).toEqual({ ok: false, reason: "invalid" });
    expect(await markDmRead(toUserWireId(a.id), "not-a-wire-id", 1)).toEqual({ ok: false, reason: "invalid" });
  });

  it("rejects a peer with no conversation at all", async () => {
    const a = await insertUser("1");
    const b = await insertUser("2");
    expect(await markDmRead(toUserWireId(a.id), toUserWireId(b.id), 1)).toEqual({
      ok: false,
      reason: "invalid"
    });
  });

  // The race this reproduces: DM_SEND (server/src/protocol/handlers/
  // DMHandler.cpp) broadcasts DM_MESSAGE synchronously, with the
  // message_id already assigned, then enqueues persistence to
  // MessagePersistenceWorker — fire-and-forget, bounded retry
  // (§3.4/§4.5's Option B). A client can legitimately send MARK_DM_READ
  // for a message_id it just received before that worker's write lands.
  // markDmRead must not reject that: DMReadStateHandler (C++) already
  // bounds up_to_message_id against the server's own in-memory counter —
  // the one source of truth that's never behind a message_id a client
  // has actually been given — before ever calling this function, so this
  // layer only needs a conversation to exist, not a persisted row for
  // this exact message.
  it("accepts an up_to_message_id for a message that hasn't been persisted yet (still in the fire-and-forget persistence queue)", async () => {
    const a = await insertUser("1");
    const b = await insertUser("2");
    const conversationId = await resolveOrCreateDmConversationId(a.id, b.id);
    await sendMessage(conversationId, b.id); // one real, persisted message
    const notYetPersistedSeq = nextSeq++; // the next message_id DM_SEND would hand out — no row for it

    expect(await markDmRead(toUserWireId(a.id), toUserWireId(b.id), notYetPersistedSeq)).toEqual({
      ok: true,
      markerSeq: notYetPersistedSeq,
      unread: 0
    });
  });

  // Documents the deliberate narrowing of what this layer validates (the
  // bound check moved to DMReadStateHandler, C++): once a conversation
  // exists, this function trusts up_to_message_id as given, even one
  // that happens to be a real seq from a *different* conversation. Safe
  // because a marker only ever affects the caller's own read state for
  // their own conversation with this peer — it can't leak or alter
  // anyone else's data, only mis-set the caller's own marker.
  it("accepts an up_to_message_id that is a real seq from a different conversation, once a conversation with this peer exists", async () => {
    const a = await insertUser("1");
    const b = await insertUser("2");
    const c = await insertUser("3");
    await resolveOrCreateDmConversationId(a.id, b.id);
    const otherConversation = await resolveOrCreateDmConversationId(a.id, c.id);
    const foreignSeq = await sendMessage(otherConversation, a.id);

    expect(await markDmRead(toUserWireId(a.id), toUserWireId(b.id), foreignSeq)).toEqual({
      ok: true,
      markerSeq: foreignSeq,
      unread: 0
    });
  });

  it("marks read and reports 0 unread when nothing newer exists", async () => {
    const a = await insertUser("1");
    const b = await insertUser("2");
    const conversationId = await resolveOrCreateDmConversationId(a.id, b.id);
    const seq = await sendMessage(conversationId, b.id);

    expect(await markDmRead(toUserWireId(a.id), toUserWireId(b.id), seq)).toEqual({
      ok: true,
      markerSeq: seq,
      unread: 0
    });
    const [row] = await db.select().from(dmReadState);
    expect(row.lastReadSeq).toBe(seq);
  });

  it("reports unread messages sent by the peer after the marker, excluding the caller's own", async () => {
    const a = await insertUser("1");
    const b = await insertUser("2");
    const conversationId = await resolveOrCreateDmConversationId(a.id, b.id);
    const firstSeq = await sendMessage(conversationId, b.id);
    await sendMessage(conversationId, a.id); // the caller's own — never unread to the caller
    await sendMessage(conversationId, b.id);
    await sendMessage(conversationId, b.id);

    expect(await markDmRead(toUserWireId(a.id), toUserWireId(b.id), firstSeq)).toEqual({
      ok: true,
      markerSeq: firstSeq,
      unread: 2
    });
  });

  it("is monotonic: a lower up_to_message_id never moves the marker backward, and still succeeds", async () => {
    const a = await insertUser("1");
    const b = await insertUser("2");
    const conversationId = await resolveOrCreateDmConversationId(a.id, b.id);
    const firstSeq = await sendMessage(conversationId, b.id);
    const secondSeq = await sendMessage(conversationId, b.id);

    expect(await markDmRead(toUserWireId(a.id), toUserWireId(b.id), secondSeq)).toEqual({
      ok: true,
      markerSeq: secondSeq,
      unread: 0
    });
    // Retried/out-of-order call with the earlier seq — idempotent, not an error.
    expect(await markDmRead(toUserWireId(a.id), toUserWireId(b.id), firstSeq)).toEqual({
      ok: true,
      markerSeq: secondSeq,
      unread: 0
    });
    const [row] = await db.select().from(dmReadState);
    expect(row.lastReadSeq).toBe(secondSeq);
  });

  it("caps unread at 100", async () => {
    const a = await insertUser("1");
    const b = await insertUser("2");
    const conversationId = await resolveOrCreateDmConversationId(a.id, b.id);
    const markerSeq = await sendMessage(conversationId, b.id);
    for (let i = 0; i < 105; i++) {
      await sendMessage(conversationId, b.id);
    }

    expect(await markDmRead(toUserWireId(a.id), toUserWireId(b.id), markerSeq)).toEqual({
      ok: true,
      markerSeq,
      unread: 100
    });
  }, 20_000);
});

describe("fetchUnreadCounts", () => {
  it("returns null for a malformed user id", async () => {
    expect(await fetchUnreadCounts("not-a-wire-id")).toBeNull();
  });

  it("returns an empty array for a user with no conversations", async () => {
    const a = await insertUser("1");
    expect(await fetchUnreadCounts(toUserWireId(a.id))).toEqual([]);
  });

  it("counts a brand-new conversation with no dm_read_state row at all", async () => {
    // The bug this guards: a query that starts from dm_read_state (instead
    // of dm_conversations, LEFT JOINing read state) would never see a
    // conversation that has never been marked read even once, and its
    // first messages would silently never count as unread.
    const a = await insertUser("1");
    const b = await insertUser("2");
    const conversationId = await resolveOrCreateDmConversationId(a.id, b.id);
    await sendMessage(conversationId, b.id);
    await sendMessage(conversationId, b.id);
    expect(await db.select().from(dmReadState)).toHaveLength(0);

    expect(await fetchUnreadCounts(toUserWireId(a.id))).toEqual([{ peer_id: toUserWireId(b.id), count: 2 }]);
  });

  it("omits a conversation with 0 unread and reports several conversations independently", async () => {
    const a = await insertUser("1");
    const b = await insertUser("2");
    const c = await insertUser("3");
    const withB = await resolveOrCreateDmConversationId(a.id, b.id);
    const withC = await resolveOrCreateDmConversationId(a.id, c.id);

    const readSeq = await sendMessage(withB, b.id);
    await markDmRead(toUserWireId(a.id), toUserWireId(b.id), readSeq); // caught up with b

    await sendMessage(withC, c.id);
    await sendMessage(withC, c.id);
    await sendMessage(withC, a.id); // a's own — not unread to a

    expect(await fetchUnreadCounts(toUserWireId(a.id))).toEqual([{ peer_id: toUserWireId(c.id), count: 2 }]);
  });

  it("caps a single conversation's count at 100", async () => {
    const a = await insertUser("1");
    const b = await insertUser("2");
    const conversationId = await resolveOrCreateDmConversationId(a.id, b.id);
    for (let i = 0; i < 105; i++) {
      await sendMessage(conversationId, b.id);
    }

    expect(await fetchUnreadCounts(toUserWireId(a.id))).toEqual([{ peer_id: toUserWireId(b.id), count: 100 }]);
  }, 20_000);
});
