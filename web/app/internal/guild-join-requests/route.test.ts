import { sql } from "drizzle-orm";
import { NextRequest } from "next/server";
import { afterEach, beforeAll, describe, expect, it } from "vitest";

import { db } from "@/db/client";
import { channels, guildInvites, guildJoinRequests, guildMemberships, guilds, users } from "@/db/schema";
import { createGuild } from "@/lib/internal/catalog";
import { toUserWireId } from "@/lib/internal/wireIds";

import { GET, POST } from "./route";

const URL = "http://internal/internal/guild-join-requests";
const SECRET_HEADERS = { "x-internal-secret": "test-internal-secret", "content-type": "application/json" };

beforeAll(() => {
  process.env.INTERNAL_API_SHARED_SECRET = "test-internal-secret";
});

afterEach(async () => {
  await db.execute(
    sql`TRUNCATE TABLE ${channels}, ${guildInvites}, ${guildJoinRequests}, ${guildMemberships}, ${guilds}, ${users} RESTART IDENTITY CASCADE`
  );
});

describe("POST /internal/guild-join-requests", () => {
  it("returns 401 without the shared secret", async () => {
    const request = new NextRequest(URL, { method: "POST", body: "{}" });
    expect((await POST(request)).status).toBe(401);
  });

  it("creates a request", async () => {
    const [owner] = await db
      .insert(users)
      .values({ discordId: "1", discordUsername: "alice" })
      .returning();
    const [requester] = await db
      .insert(users)
      .values({ discordId: "2", discordUsername: "bob" })
      .returning();
    const guild = await createGuild("My Guild", toUserWireId(owner.id), "application");

    const request = new NextRequest(URL, {
      method: "POST",
      headers: SECRET_HEADERS,
      body: JSON.stringify({ guild_id: guild.guild_id, user_id: toUserWireId(requester.id) })
    });
    const response = await POST(request);
    expect(response.status).toBe(201);
    const body = (await response.json()) as { result: string };
    expect(body.result).toBe("created");
  });

  it("returns 400 for a missing guild_id", async () => {
    const request = new NextRequest(URL, {
      method: "POST",
      headers: SECRET_HEADERS,
      body: JSON.stringify({ user_id: "u_x" })
    });
    expect((await POST(request)).status).toBe(400);
  });
});

describe("GET /internal/guild-join-requests", () => {
  it("returns 401 without the shared secret", async () => {
    const request = new NextRequest(`${URL}?guild_id=g_x`, { method: "GET" });
    expect((await GET(request)).status).toBe(401);
  });

  it("returns 404 for a guild that doesn't exist", async () => {
    const request = new NextRequest(`${URL}?guild_id=g_00000000-0000-0000-0000-000000000000`, {
      method: "GET",
      headers: SECRET_HEADERS
    });
    expect((await GET(request)).status).toBe(404);
  });

  it("returns the pending requests", async () => {
    const [owner] = await db
      .insert(users)
      .values({ discordId: "3", discordUsername: "carol" })
      .returning();
    const [requester] = await db
      .insert(users)
      .values({ discordId: "4", discordUsername: "dave" })
      .returning();
    const guild = await createGuild("My Guild", toUserWireId(owner.id), "application");
    await POST(
      new NextRequest(URL, {
        method: "POST",
        headers: SECRET_HEADERS,
        body: JSON.stringify({ guild_id: guild.guild_id, user_id: toUserWireId(requester.id) })
      })
    );

    const request = new NextRequest(`${URL}?guild_id=${guild.guild_id}`, {
      method: "GET",
      headers: SECRET_HEADERS
    });
    const response = await GET(request);
    expect(response.status).toBe(200);
    const body = (await response.json()) as { requests: Array<{ username: string }> };
    expect(body.requests).toHaveLength(1);
    expect(body.requests[0].username).toBe("dave");
  });
});
