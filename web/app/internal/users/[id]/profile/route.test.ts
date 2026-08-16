import { sql } from "drizzle-orm";
import { NextRequest } from "next/server";
import { afterEach, beforeAll, describe, expect, it } from "vitest";

import { db } from "@/db/client";
import { users } from "@/db/schema";
import { toUserWireId } from "@/lib/internal/wireIds";

import { GET } from "./route";

const SECRET_HEADERS = { "x-internal-secret": "test-internal-secret" };

beforeAll(() => {
  process.env.INTERNAL_API_SHARED_SECRET = "test-internal-secret";
});

afterEach(async () => {
  await db.execute(sql`TRUNCATE TABLE ${users} RESTART IDENTITY CASCADE`);
});

describe("GET /internal/users/:id/profile", () => {
  it("returns 401 without the shared secret", async () => {
    const request = new NextRequest("http://internal/x");
    const response = await GET(request, { params: Promise.resolve({ id: "u_x" }) });
    expect(response.status).toBe(401);
  });

  it("returns the resolved profile summary", async () => {
    const [a] = await db.insert(users).values({ discordId: "1", discordUsername: "alice" }).returning();

    const request = new NextRequest("http://internal/x", { headers: SECRET_HEADERS });
    const response = await GET(request, { params: Promise.resolve({ id: toUserWireId(a.id) }) });
    expect(response.status).toBe(200);
    expect(await response.json()).toEqual({
      user_id: toUserWireId(a.id),
      username: "alice",
      display_name: "alice",
      avatar_url: null
    });
  });

  it("resolves a customized display_name/avatar over the Discord fallback", async () => {
    const [a] = await db
      .insert(users)
      .values({ discordId: "1", discordUsername: "alice", displayName: "Al", customAvatarPath: "/uploads/avatars/x.png" })
      .returning();

    const request = new NextRequest("http://internal/x", { headers: SECRET_HEADERS });
    const response = await GET(request, { params: Promise.resolve({ id: toUserWireId(a.id) }) });
    expect(await response.json()).toEqual({
      user_id: toUserWireId(a.id),
      username: "alice",
      display_name: "Al",
      avatar_url: "/uploads/avatars/x.png"
    });
  });

  it("returns 404 for a nonexistent user", async () => {
    const request = new NextRequest("http://internal/x", { headers: SECRET_HEADERS });
    const response = await GET(request, {
      params: Promise.resolve({ id: "u_00000000-0000-0000-0000-000000000000" })
    });
    expect(response.status).toBe(404);
  });

  it("returns 404 for a malformed user id", async () => {
    const request = new NextRequest("http://internal/x", { headers: SECRET_HEADERS });
    const response = await GET(request, { params: Promise.resolve({ id: "not-a-wire-id" }) });
    expect(response.status).toBe(404);
  });
});
