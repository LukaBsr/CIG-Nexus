import { sql } from "drizzle-orm";
import { NextRequest } from "next/server";
import { afterEach, beforeAll, describe, expect, it } from "vitest";

import { db } from "@/db/client";
import { guildMemberships, guilds, users } from "@/db/schema";
import { createGuild } from "@/lib/internal/catalog";
import { toUserWireId } from "@/lib/internal/wireIds";

import { POST } from "./route";

const URL = "http://internal/api/internal/guild-memberships";
const SECRET_HEADERS = { "x-internal-secret": "test-internal-secret", "content-type": "application/json" };

beforeAll(() => {
  process.env.INTERNAL_API_SHARED_SECRET = "test-internal-secret";
});

afterEach(async () => {
  await db.execute(sql`TRUNCATE TABLE ${guildMemberships}, ${guilds}, ${users} RESTART IDENTITY CASCADE`);
});

describe("POST /api/internal/guild-memberships", () => {
  it("returns 401 without the shared secret", async () => {
    const request = new NextRequest(URL, { method: "POST", body: "{}" });
    expect((await POST(request)).status).toBe(401);
  });

  it("adds a member to an existing guild", async () => {
    const [owner] = await db
      .insert(users)
      .values({ discordId: "1", discordUsername: "alice" })
      .returning();
    const [member] = await db
      .insert(users)
      .values({ discordId: "2", discordUsername: "bob" })
      .returning();
    const guild = await createGuild("My Guild", toUserWireId(owner.id));

    const request = new NextRequest(URL, {
      method: "POST",
      headers: SECRET_HEADERS,
      body: JSON.stringify({ guild_id: guild.guild_id, user_id: toUserWireId(member.id) })
    });
    const response = await POST(request);
    expect(response.status).toBe(201);
  });

  it("returns 400 for a duplicate membership", async () => {
    const [owner] = await db
      .insert(users)
      .values({ discordId: "3", discordUsername: "carol" })
      .returning();
    const guild = await createGuild("My Guild", toUserWireId(owner.id));

    const request = new NextRequest(URL, {
      method: "POST",
      headers: SECRET_HEADERS,
      body: JSON.stringify({ guild_id: guild.guild_id, user_id: toUserWireId(owner.id) })
    });
    expect((await POST(request)).status).toBe(400);
  });

  it("returns 400 for an invalid role", async () => {
    const request = new NextRequest(URL, {
      method: "POST",
      headers: SECRET_HEADERS,
      body: JSON.stringify({ guild_id: "g_x", user_id: "u_x", role: "admin" })
    });
    expect((await POST(request)).status).toBe(400);
  });
});
