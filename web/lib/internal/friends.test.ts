import { sql } from "drizzle-orm";
import { afterEach, describe, expect, it } from "vitest";

import { db } from "@/db/client";
import { friendRequests, friendships, userBlocks, users } from "@/db/schema";

import { blockUser } from "./blocks";
import {
  acceptFriendRequest,
  addFriendByCode,
  deleteFriendRequest,
  fetchFriendCode,
  listFriendRequests,
  listFriends,
  regenerateFriendCode,
  removeFriend,
  sendFriendRequest
} from "./friends";
import { toUserWireId } from "./wireIds";

afterEach(async () => {
  await db.execute(
    sql`TRUNCATE TABLE ${friendRequests}, ${friendships}, ${userBlocks}, ${users} RESTART IDENTITY CASCADE`
  );
});

async function insertUser(discordId: string) {
  const [user] = await db
    .insert(users)
    .values({ discordId, discordUsername: `user_${discordId}` })
    .returning();
  return user;
}

describe("sendFriendRequest", () => {
  it("creates a pending request", async () => {
    const a = await insertUser("1");
    const b = await insertUser("2");

    const result = await sendFriendRequest(toUserWireId(a.id), toUserWireId(b.id));

    expect(result).toEqual({ ok: true, kind: "request", user_id: toUserWireId(b.id), username: "user_2" });
    const rows = await db.select().from(friendRequests);
    expect(rows).toHaveLength(1);
    expect(rows[0].requesterId).toBe(a.id);
    expect(rows[0].recipientId).toBe(b.id);
  });

  it("rejects a request targeting self", async () => {
    const a = await insertUser("1");
    const result = await sendFriendRequest(toUserWireId(a.id), toUserWireId(a.id));
    expect(result).toEqual({ ok: false, error: "self" });
  });

  it("rejects a request targeting a nonexistent user", async () => {
    const a = await insertUser("1");
    const result = await sendFriendRequest(toUserWireId(a.id), "u_00000000-0000-0000-0000-000000000000");
    expect(result).toEqual({ ok: false, error: "user_not_found" });
  });

  it("rejects a request when already friends", async () => {
    const a = await insertUser("1");
    const b = await insertUser("2");
    await sendFriendRequest(toUserWireId(a.id), toUserWireId(b.id));
    await acceptFriendRequest(toUserWireId(b.id), toUserWireId(a.id));

    const result = await sendFriendRequest(toUserWireId(a.id), toUserWireId(b.id));
    expect(result).toEqual({ ok: false, error: "already_friends" });
  });

  it("auto-accepts when a reverse-pending request already exists", async () => {
    const a = await insertUser("1");
    const b = await insertUser("2");
    await sendFriendRequest(toUserWireId(a.id), toUserWireId(b.id)); // a -> b pending

    const result = await sendFriendRequest(toUserWireId(b.id), toUserWireId(a.id)); // b -> a

    expect(result).toEqual({ ok: true, kind: "friends", user_id: toUserWireId(a.id), username: "user_1" });
    const pending = await db.select().from(friendRequests);
    expect(pending).toHaveLength(0);
    const friendRows = await db.select().from(friendships);
    expect(friendRows).toHaveLength(1);
  });

  it("is idempotent against a duplicate concurrent send (unique constraint, no throw)", async () => {
    const a = await insertUser("1");
    const b = await insertUser("2");
    await sendFriendRequest(toUserWireId(a.id), toUserWireId(b.id));
    await sendFriendRequest(toUserWireId(a.id), toUserWireId(b.id));

    const rows = await db.select().from(friendRequests);
    expect(rows).toHaveLength(1);
  });
});

describe("addFriendByCode", () => {
  it("sends a request to the code's owner", async () => {
    const a = await insertUser("1");
    const b = await insertUser("2");
    await db.update(users).set({ friendCode: "code123" }).where(sql`${users.id} = ${b.id}`);

    const result = await addFriendByCode(toUserWireId(a.id), "code123");
    expect(result).toEqual({ ok: true, kind: "request", user_id: toUserWireId(b.id), username: "user_2" });
  });

  it("returns code_not_found for an unknown code", async () => {
    const a = await insertUser("1");
    const result = await addFriendByCode(toUserWireId(a.id), "no-such-code");
    expect(result).toEqual({ ok: false, error: "code_not_found" });
  });
});

describe("acceptFriendRequest / deleteFriendRequest", () => {
  it("accept creates a friendship and removes the pending request", async () => {
    const a = await insertUser("1");
    const b = await insertUser("2");
    await sendFriendRequest(toUserWireId(a.id), toUserWireId(b.id));

    const result = await acceptFriendRequest(toUserWireId(b.id), toUserWireId(a.id));
    expect(result).toEqual({ ok: true, user_id: toUserWireId(a.id), username: "user_1" });

    const pending = await db.select().from(friendRequests);
    expect(pending).toHaveLength(0);
    const friends = await listFriends(toUserWireId(a.id));
    expect(friends).toEqual([{ user_id: toUserWireId(b.id), username: "user_2" }]);
  });

  it("accept returns not_found for a nonexistent request", async () => {
    const a = await insertUser("1");
    const b = await insertUser("2");
    const result = await acceptFriendRequest(toUserWireId(b.id), toUserWireId(a.id));
    expect(result).toEqual({ ok: false, error: "not_found" });
  });

  it("deleteFriendRequest removes the row regardless of reject-vs-cancel framing", async () => {
    const a = await insertUser("1");
    const b = await insertUser("2");
    await sendFriendRequest(toUserWireId(a.id), toUserWireId(b.id));

    const deleted = await deleteFriendRequest(toUserWireId(a.id), toUserWireId(b.id));
    expect(deleted).toBe(true);
    const rows = await db.select().from(friendRequests);
    expect(rows).toHaveLength(0);
  });

  it("deleteFriendRequest returns false for a nonexistent pair", async () => {
    const a = await insertUser("1");
    const b = await insertUser("2");
    const deleted = await deleteFriendRequest(toUserWireId(a.id), toUserWireId(b.id));
    expect(deleted).toBe(false);
  });
});

describe("removeFriend", () => {
  it("removes an existing friendship regardless of argument order", async () => {
    const a = await insertUser("1");
    const b = await insertUser("2");
    await sendFriendRequest(toUserWireId(a.id), toUserWireId(b.id));
    await acceptFriendRequest(toUserWireId(b.id), toUserWireId(a.id));

    const removed = await removeFriend(toUserWireId(b.id), toUserWireId(a.id));
    expect(removed).toBe(true);
    expect(await db.select().from(friendships)).toHaveLength(0);
  });

  it("returns false when no friendship exists", async () => {
    const a = await insertUser("1");
    const b = await insertUser("2");
    expect(await removeFriend(toUserWireId(a.id), toUserWireId(b.id))).toBe(false);
  });
});

describe("listFriends", () => {
  it("returns the other party regardless of canonical ordering", async () => {
    const a = await insertUser("1");
    const b = await insertUser("2");
    const c = await insertUser("3");
    await sendFriendRequest(toUserWireId(a.id), toUserWireId(b.id));
    await acceptFriendRequest(toUserWireId(b.id), toUserWireId(a.id));
    await sendFriendRequest(toUserWireId(c.id), toUserWireId(a.id));
    await acceptFriendRequest(toUserWireId(a.id), toUserWireId(c.id));

    const friends = await listFriends(toUserWireId(a.id));
    expect(friends).toHaveLength(2);
    expect(friends?.map((f) => f.user_id).sort()).toEqual([toUserWireId(b.id), toUserWireId(c.id)].sort());
  });

  it("returns an empty array, not null, for a user with no friends", async () => {
    const a = await insertUser("1");
    expect(await listFriends(toUserWireId(a.id))).toEqual([]);
  });
});

describe("listFriendRequests", () => {
  it("separates incoming from outgoing", async () => {
    const a = await insertUser("1");
    const b = await insertUser("2");
    const c = await insertUser("3");
    await sendFriendRequest(toUserWireId(a.id), toUserWireId(b.id)); // a -> b (a's outgoing)
    await sendFriendRequest(toUserWireId(c.id), toUserWireId(a.id)); // c -> a (a's incoming)

    const result = await listFriendRequests(toUserWireId(a.id));
    expect(result?.outgoing).toEqual([{ user_id: toUserWireId(b.id), username: "user_2", created_at: expect.any(String) }]);
    expect(result?.incoming).toEqual([{ user_id: toUserWireId(c.id), username: "user_3", created_at: expect.any(String) }]);
  });
});

describe("friend codes", () => {
  it("fetchFriendCode backfills a missing code rather than erroring", async () => {
    const a = await insertUser("1");
    await db.update(users).set({ friendCode: null }).where(sql`${users.id} = ${a.id}`);

    const code = await fetchFriendCode(toUserWireId(a.id));
    expect(code).not.toBeNull();
    expect(typeof code).toBe("string");
  });

  it("regenerateFriendCode replaces the stored code", async () => {
    const a = await insertUser("1");
    const before = await fetchFriendCode(toUserWireId(a.id));
    const after = await regenerateFriendCode(toUserWireId(a.id));
    expect(after).not.toBe(before);

    const [row] = await db.select({ friendCode: users.friendCode }).from(users).where(sql`${users.id} = ${a.id}`);
    expect(row.friendCode).toBe(after);
  });
});

// docs/social/friends-dms-design.md §1.4 step 3 / §2.6: blocked reuses the
// same "user_not_found" discriminant as a nonexistent target, in both
// directions and via both entry points (direct user_id and by code).
describe("sendFriendRequest / addFriendByCode with an active block", () => {
  it("rejects when the caller has blocked the target", async () => {
    const a = await insertUser("1");
    const b = await insertUser("2");
    await blockUser(toUserWireId(a.id), toUserWireId(b.id));

    const result = await sendFriendRequest(toUserWireId(a.id), toUserWireId(b.id));
    expect(result).toEqual({ ok: false, error: "user_not_found" });
  });

  it("rejects when the target has blocked the caller", async () => {
    const a = await insertUser("1");
    const b = await insertUser("2");
    await blockUser(toUserWireId(b.id), toUserWireId(a.id));

    const result = await sendFriendRequest(toUserWireId(a.id), toUserWireId(b.id));
    expect(result).toEqual({ ok: false, error: "user_not_found" });
  });

  it("rejects ADD_FRIEND_BY_CODE the same way when blocked", async () => {
    const a = await insertUser("1");
    const b = await insertUser("2");
    await db.update(users).set({ friendCode: "code123" }).where(sql`${users.id} = ${b.id}`);
    await blockUser(toUserWireId(b.id), toUserWireId(a.id));

    const result = await addFriendByCode(toUserWireId(a.id), "code123");
    expect(result).toEqual({ ok: false, error: "user_not_found" });
  });
});
