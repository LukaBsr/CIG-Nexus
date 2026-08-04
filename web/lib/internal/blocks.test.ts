import { sql } from "drizzle-orm";
import { afterEach, describe, expect, it } from "vitest";

import { db } from "@/db/client";
import { friendRequests, friendships, userBlocks, users } from "@/db/schema";

import { blockUser, isBlockedBy, isBlockedEitherDirection, listBlocks, unblockUser } from "./blocks";
import { acceptFriendRequest, sendFriendRequest } from "./friends";
import { toUserWireId } from "./wireIds";

afterEach(async () => {
  await db.execute(
    sql`TRUNCATE TABLE ${userBlocks}, ${friendRequests}, ${friendships}, ${users} RESTART IDENTITY CASCADE`
  );
});

async function insertUser(discordId: string) {
  const [user] = await db
    .insert(users)
    .values({ discordId, discordUsername: `user_${discordId}` })
    .returning();
  return user;
}

describe("blockUser", () => {
  it("creates a block row", async () => {
    const a = await insertUser("1");
    const b = await insertUser("2");
    expect(await blockUser(toUserWireId(a.id), toUserWireId(b.id))).toBe(true);
    const rows = await db.select().from(userBlocks);
    expect(rows).toHaveLength(1);
    expect(rows[0].blockerId).toBe(a.id);
    expect(rows[0].blockedId).toBe(b.id);
  });

  it("is idempotent against blocking an already-blocked user", async () => {
    const a = await insertUser("1");
    const b = await insertUser("2");
    await blockUser(toUserWireId(a.id), toUserWireId(b.id));
    expect(await blockUser(toUserWireId(a.id), toUserWireId(b.id))).toBe(true);
    expect(await db.select().from(userBlocks)).toHaveLength(1);
  });

  it("rejects blocking self", async () => {
    const a = await insertUser("1");
    expect(await blockUser(toUserWireId(a.id), toUserWireId(a.id))).toBe(false);
  });

  it("cancels a pending friend request in either direction", async () => {
    const a = await insertUser("1");
    const b = await insertUser("2");
    await sendFriendRequest(toUserWireId(a.id), toUserWireId(b.id));
    expect(await db.select().from(friendRequests)).toHaveLength(1);

    await blockUser(toUserWireId(a.id), toUserWireId(b.id));
    expect(await db.select().from(friendRequests)).toHaveLength(0);
  });

  it("removes an existing friendship", async () => {
    const a = await insertUser("1");
    const b = await insertUser("2");
    await sendFriendRequest(toUserWireId(a.id), toUserWireId(b.id));
    await acceptFriendRequest(toUserWireId(b.id), toUserWireId(a.id));
    expect(await db.select().from(friendships)).toHaveLength(1);

    await blockUser(toUserWireId(b.id), toUserWireId(a.id));
    expect(await db.select().from(friendships)).toHaveLength(0);
  });
});

describe("unblockUser", () => {
  it("removes the block row", async () => {
    const a = await insertUser("1");
    const b = await insertUser("2");
    await blockUser(toUserWireId(a.id), toUserWireId(b.id));
    expect(await unblockUser(toUserWireId(a.id), toUserWireId(b.id))).toBe(true);
    expect(await db.select().from(userBlocks)).toHaveLength(0);
  });

  it("returns false when there's nothing to unblock", async () => {
    const a = await insertUser("1");
    const b = await insertUser("2");
    expect(await unblockUser(toUserWireId(a.id), toUserWireId(b.id))).toBe(false);
  });
});

describe("listBlocks", () => {
  it("lists blocked users", async () => {
    const a = await insertUser("1");
    const b = await insertUser("2");
    await blockUser(toUserWireId(a.id), toUserWireId(b.id));

    const blocks = await listBlocks(toUserWireId(a.id));
    expect(blocks).toEqual([{ user_id: toUserWireId(b.id), username: "user_2", blocked_at: expect.any(String) }]);
  });
});

describe("isBlockedEitherDirection / isBlockedBy", () => {
  it("isBlockedEitherDirection is true regardless of which side blocked", async () => {
    const a = await insertUser("1");
    const b = await insertUser("2");
    await blockUser(toUserWireId(b.id), toUserWireId(a.id)); // b blocked a
    expect(await isBlockedEitherDirection(a.id, b.id)).toBe(true);
    expect(await isBlockedEitherDirection(b.id, a.id)).toBe(true);
  });

  it("isBlockedBy is one-directional", async () => {
    const a = await insertUser("1");
    const b = await insertUser("2");
    await blockUser(toUserWireId(b.id), toUserWireId(a.id)); // b blocked a
    expect(await isBlockedBy(b.id, a.id)).toBe(true);
    expect(await isBlockedBy(a.id, b.id)).toBe(false);
  });
});
