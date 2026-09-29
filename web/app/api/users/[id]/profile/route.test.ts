import { sql } from "drizzle-orm";
import { NextRequest } from "next/server";
import { afterEach, describe, expect, it } from "vitest";

import { db } from "@/db/client";
import { friendships, guildMemberships, guilds, sessions, userBlocks, users } from "@/db/schema";
import { createSession } from "@/lib/auth/session";
import { toUserWireId } from "@/lib/internal/wireIds";
import { kOwnerRank } from "@/lib/internal/roleThemes";

import { GET } from "./route";

afterEach(async () => {
  await db.execute(
    sql`TRUNCATE TABLE ${sessions}, ${userBlocks}, ${friendships}, ${guildMemberships}, ${guilds}, ${users} RESTART IDENTITY CASCADE`
  );
});

async function insertUser(discordId: string, extra?: Partial<typeof users.$inferInsert>) {
  const [user] = await db
    .insert(users)
    .values({ discordId, discordUsername: `user_${discordId}`, ...extra })
    .returning();
  return user;
}

async function makeFriends(userIdA: string, userIdB: string) {
  const [a, b] = userIdA < userIdB ? [userIdA, userIdB] : [userIdB, userIdA];
  await db.insert(friendships).values({ userIdA: a, userIdB: b });
}

async function addToSharedGuild(userIdA: string, userIdB: string) {
  const [guild] = await db.insert(guilds).values({ name: "Shared Guild", ownerId: userIdA }).returning();
  await db.insert(guildMemberships).values([
    { guildId: guild.id, userId: userIdA, roleRank: kOwnerRank },
    { guildId: guild.id, userId: userIdB }
  ]);
}

function getRequest(cookie?: string): NextRequest {
  return new NextRequest("http://localhost:3000/api/users/u_x/profile", {
    headers: new Headers(cookie ? { cookie } : {})
  });
}

describe("GET /api/users/[id]/profile", () => {
  it("returns 401 when there is no refresh cookie", async () => {
    const user = await insertUser("1");
    const response = await GET(getRequest(), { params: Promise.resolve({ id: toUserWireId(user.id) }) });
    expect(response.status).toBe(401);
  });

  it("returns 404 for a malformed wire id", async () => {
    const caller = await insertUser("2");
    const { refreshToken } = await createSession(caller.id, {});

    const response = await GET(getRequest(`__session=${refreshToken}`), {
      params: Promise.resolve({ id: "not-a-wire-id" })
    });
    expect(response.status).toBe(404);
  });

  it("returns 404 for a well-formed but nonexistent user id", async () => {
    const caller = await insertUser("3");
    const { refreshToken } = await createSession(caller.id, {});

    const response = await GET(getRequest(`__session=${refreshToken}`), {
      params: Promise.resolve({ id: "u_00000000-0000-0000-0000-000000000000" })
    });
    expect(response.status).toBe(404);
  });

  it("returns 404 when the caller and target have no friend or shared-guild relationship (§4.6)", async () => {
    const caller = await insertUser("4");
    const target = await insertUser("5");
    const { refreshToken } = await createSession(caller.id, {});

    const response = await GET(getRequest(`__session=${refreshToken}`), {
      params: Promise.resolve({ id: toUserWireId(target.id) })
    });
    expect(response.status).toBe(404);
  });

  it("returns the target's public profile fields when the caller and target are friends", async () => {
    const caller = await insertUser("6");
    const target = await insertUser("7", { bio: "hello", statusMessage: "afk", accentColor: "#5eead4" });
    await makeFriends(caller.id, target.id);
    const { refreshToken } = await createSession(caller.id, {});

    const response = await GET(getRequest(`__session=${refreshToken}`), {
      params: Promise.resolve({ id: toUserWireId(target.id) })
    });
    expect(response.status).toBe(200);
    const body = (await response.json()) as {
      user_id: string;
      display_name: string;
      bio: string | null;
      status_message: string | null;
      accent_color: string | null;
      avatar_url: string | null;
    };
    expect(body).toEqual({
      user_id: toUserWireId(target.id),
      display_name: "user_7",
      bio: "hello",
      status_message: "afk",
      accent_color: "#5eead4",
      avatar_url: null
    });
  });

  it("returns the target's profile when the caller and target only share a guild, not friends", async () => {
    const caller = await insertUser("8");
    const target = await insertUser("9");
    await addToSharedGuild(caller.id, target.id);
    const { refreshToken } = await createSession(caller.id, {});

    const response = await GET(getRequest(`__session=${refreshToken}`), {
      params: Promise.resolve({ id: toUserWireId(target.id) })
    });
    expect(response.status).toBe(200);
  });

  it("returns 404 (indistinguishable from nonexistent) when the target has blocked the caller, even though they're friends", async () => {
    const caller = await insertUser("10");
    const target = await insertUser("11");
    await makeFriends(caller.id, target.id);
    await db.insert(userBlocks).values({ blockerId: target.id, blockedId: caller.id });
    const { refreshToken } = await createSession(caller.id, {});

    const response = await GET(getRequest(`__session=${refreshToken}`), {
      params: Promise.resolve({ id: toUserWireId(target.id) })
    });
    expect(response.status).toBe(404);
  });

  it("returns 404 when the caller has blocked the target, even though they're friends (either-direction, §4.6)", async () => {
    const caller = await insertUser("12");
    const target = await insertUser("13");
    await makeFriends(caller.id, target.id);
    await db.insert(userBlocks).values({ blockerId: caller.id, blockedId: target.id });
    const { refreshToken } = await createSession(caller.id, {});

    const response = await GET(getRequest(`__session=${refreshToken}`), {
      params: Promise.resolve({ id: toUserWireId(target.id) })
    });
    expect(response.status).toBe(404);
  });

  it("returns byte-identical 404 bodies for nonexistent, blocked, and no-relationship targets (enumeration resistance, §4.6)", async () => {
    const caller = await insertUser("14");
    const friend = await insertUser("15");
    await makeFriends(caller.id, friend.id);
    await db.insert(userBlocks).values({ blockerId: friend.id, blockedId: caller.id });
    const stranger = await insertUser("16");
    const { refreshToken } = await createSession(caller.id, {});
    const cookie = `__session=${refreshToken}`;

    const [nonexistent, blockedByFriend, noRelationship] = await Promise.all([
      GET(getRequest(cookie), { params: Promise.resolve({ id: "u_00000000-0000-0000-0000-000000000000" }) }),
      GET(getRequest(cookie), { params: Promise.resolve({ id: toUserWireId(friend.id) }) }),
      GET(getRequest(cookie), { params: Promise.resolve({ id: toUserWireId(stranger.id) }) })
    ]);

    expect(nonexistent.status).toBe(404);
    expect(blockedByFriend.status).toBe(404);
    expect(noRelationship.status).toBe(404);
    const [nonexistentBody, blockedBody, noRelationshipBody] = await Promise.all([
      nonexistent.json(),
      blockedByFriend.json(),
      noRelationship.json()
    ]);
    expect(nonexistentBody).toEqual(blockedBody);
    expect(blockedBody).toEqual(noRelationshipBody);
  });

  it("returns the caller's own profile with no friend/shared-guild relationship to anyone (self-view exception, §4.6)", async () => {
    const caller = await insertUser("19", { bio: "my own bio", statusMessage: "here", accentColor: "#8b5cf6" });
    const { refreshToken } = await createSession(caller.id, {});

    const response = await GET(getRequest(`__session=${refreshToken}`), {
      params: Promise.resolve({ id: toUserWireId(caller.id) })
    });
    expect(response.status).toBe(200);
    const body = (await response.json()) as { user_id: string; bio: string | null };
    expect(body.user_id).toBe(toUserWireId(caller.id));
    expect(body.bio).toBe("my own bio");
  });

  it("returns 429 once the per-caller rate limit is exceeded", async () => {
    const caller = await insertUser("17");
    const target = await insertUser("18");
    await makeFriends(caller.id, target.id);
    const { refreshToken } = await createSession(caller.id, {});
    const cookie = `__session=${refreshToken}`;

    for (let i = 0; i < 60; i += 1) {
      const response = await GET(getRequest(cookie), { params: Promise.resolve({ id: toUserWireId(target.id) }) });
      expect(response.status).toBe(200);
    }

    const limited = await GET(getRequest(cookie), { params: Promise.resolve({ id: toUserWireId(target.id) }) });
    expect(limited.status).toBe(429);
  });
});
