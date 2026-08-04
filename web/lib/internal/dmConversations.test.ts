import { sql } from "drizzle-orm";
import { afterEach, describe, expect, it } from "vitest";

import { db } from "@/db/client";
import { dmConversations, messages, users } from "@/db/schema";

import { listDmConversations, resolveOrCreateDmConversationId } from "./dmConversations";
import { toUserWireId } from "./wireIds";

afterEach(async () => {
  await db.execute(sql`TRUNCATE TABLE ${messages}, ${dmConversations}, ${users} RESTART IDENTITY CASCADE`);
});

async function insertUser(discordId: string) {
  const [user] = await db
    .insert(users)
    .values({ discordId, discordUsername: `user_${discordId}` })
    .returning();
  return user;
}

describe("resolveOrCreateDmConversationId", () => {
  it("creates a conversation on first call", async () => {
    const a = await insertUser("1");
    const b = await insertUser("2");
    const id = await resolveOrCreateDmConversationId(a.id, b.id);
    const rows = await db.select().from(dmConversations);
    expect(rows).toHaveLength(1);
    expect(rows[0].id).toBe(id);
  });

  it("returns the same conversation regardless of argument order", async () => {
    const a = await insertUser("1");
    const b = await insertUser("2");
    const first = await resolveOrCreateDmConversationId(a.id, b.id);
    const second = await resolveOrCreateDmConversationId(b.id, a.id);
    expect(first).toBe(second);
    expect(await db.select().from(dmConversations)).toHaveLength(1);
  });

  it("stores the pair canonically ordered (userIdA < userIdB)", async () => {
    const a = await insertUser("1");
    const b = await insertUser("2");
    await resolveOrCreateDmConversationId(b.id, a.id); // reversed on purpose
    const [row] = await db.select().from(dmConversations);
    const [lo, hi] = a.id < b.id ? [a.id, b.id] : [b.id, a.id];
    expect(row.userIdA).toBe(lo);
    expect(row.userIdB).toBe(hi);
  });
});

describe("listDmConversations", () => {
  it("returns an empty array, not null, for a user with no conversations", async () => {
    const a = await insertUser("1");
    expect(await listDmConversations(toUserWireId(a.id))).toEqual([]);
  });

  it("returns null for a malformed user id", async () => {
    expect(await listDmConversations("not-a-wire-id")).toBeNull();
  });

  it("lists conversations regardless of which side the caller is on", async () => {
    const a = await insertUser("1");
    const b = await insertUser("2");
    const c = await insertUser("3");
    await resolveOrCreateDmConversationId(a.id, b.id); // a is userIdA (a < b assumed arbitrarily by uuid)
    await resolveOrCreateDmConversationId(c.id, a.id); // a could be either side here

    const conversations = await listDmConversations(toUserWireId(a.id));
    expect(conversations).toHaveLength(2);
    const peerIds = conversations?.map((c) => c.peer_id).sort();
    expect(peerIds).toEqual([toUserWireId(b.id), toUserWireId(c.id)].sort());
  });

  it("reports last_message_at as null when the conversation has no messages yet", async () => {
    const a = await insertUser("1");
    const b = await insertUser("2");
    await resolveOrCreateDmConversationId(a.id, b.id);

    const conversations = await listDmConversations(toUserWireId(a.id));
    expect(conversations).toEqual([{ peer_id: toUserWireId(b.id), last_message_at: null }]);
  });

  it("reports last_message_at from the most recent message", async () => {
    const a = await insertUser("1");
    const b = await insertUser("2");
    const conversationId = await resolveOrCreateDmConversationId(a.id, b.id);
    await db.insert(messages).values({ dmConversationId: conversationId, userId: a.id, content: "hi", seq: 1 });
    const [{ createdAt }] = await db
      .insert(messages)
      .values({ dmConversationId: conversationId, userId: b.id, content: "hey", seq: 2 })
      .returning();

    const conversations = await listDmConversations(toUserWireId(a.id));
    expect(conversations?.[0].last_message_at).toBe(createdAt.toISOString());
  });
});
