import { sql } from "drizzle-orm";
import { NextRequest } from "next/server";
import { afterEach, describe, expect, it } from "vitest";

import { db } from "@/db/client";
import { sessions, userBlocks, users } from "@/db/schema";
import { createSession } from "@/lib/auth/session";
import { toUserWireId } from "@/lib/internal/wireIds";

import { GET } from "./route";

afterEach(async () => {
  await db.execute(sql`TRUNCATE TABLE ${sessions}, ${userBlocks}, ${users} RESTART IDENTITY CASCADE`);
});

async function insertUser(discordId: string, extra?: Partial<typeof users.$inferInsert>) {
  const [user] = await db
    .insert(users)
    .values({ discordId, discordUsername: `user_${discordId}`, ...extra })
    .returning();
  return user;
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

  it("returns the target's public profile fields", async () => {
    const caller = await insertUser("4");
    const target = await insertUser("5", { bio: "hello", statusMessage: "afk", accentColor: "#5eead4" });
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
      display_name: "user_5",
      bio: "hello",
      status_message: "afk",
      accent_color: "#5eead4",
      avatar_url: null
    });
  });

  it("returns 404 (indistinguishable from nonexistent) when the target has blocked the caller", async () => {
    const caller = await insertUser("6");
    const target = await insertUser("7");
    await db.insert(userBlocks).values({ blockerId: target.id, blockedId: caller.id });
    const { refreshToken } = await createSession(caller.id, {});

    const response = await GET(getRequest(`__session=${refreshToken}`), {
      params: Promise.resolve({ id: toUserWireId(target.id) })
    });
    expect(response.status).toBe(404);
  });

  it("still allows viewing a profile when the caller blocked the target (one-directional)", async () => {
    const caller = await insertUser("8");
    const target = await insertUser("9");
    await db.insert(userBlocks).values({ blockerId: caller.id, blockedId: target.id });
    const { refreshToken } = await createSession(caller.id, {});

    const response = await GET(getRequest(`__session=${refreshToken}`), {
      params: Promise.resolve({ id: toUserWireId(target.id) })
    });
    expect(response.status).toBe(200);
  });
});
