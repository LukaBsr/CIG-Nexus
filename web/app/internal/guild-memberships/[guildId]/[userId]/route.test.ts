import { sql } from "drizzle-orm";
import { NextRequest } from "next/server";
import { afterEach, beforeAll, describe, expect, it } from "vitest";

import { db } from "@/db/client";
import { guildMemberships, guilds, users } from "@/db/schema";
import { createGuild, createMembership } from "@/lib/internal/catalog";
import { kOfficerRank } from "@/lib/internal/roleThemes";
import { toUserWireId } from "@/lib/internal/wireIds";

import { DELETE, PATCH } from "./route";

const SECRET_HEADERS = { "x-internal-secret": "test-internal-secret" };

beforeAll(() => {
  process.env.INTERNAL_API_SHARED_SECRET = "test-internal-secret";
});

afterEach(async () => {
  await db.execute(sql`TRUNCATE TABLE ${guildMemberships}, ${guilds}, ${users} RESTART IDENTITY CASCADE`);
});

describe("DELETE /internal/guild-memberships/:guildId/:userId", () => {
  it("returns 401 without the shared secret", async () => {
    const request = new NextRequest("http://internal/x", { method: "DELETE" });
    const response = await DELETE(request, { params: Promise.resolve({ guildId: "g_x", userId: "u_x" }) });
    expect(response.status).toBe(401);
  });

  it("removes an existing membership", async () => {
    const [owner] = await db
      .insert(users)
      .values({ discordId: "1", discordUsername: "alice" })
      .returning();
    const [member] = await db
      .insert(users)
      .values({ discordId: "2", discordUsername: "bob" })
      .returning();
    const guild = await createGuild("My Guild", toUserWireId(owner.id));
    await createMembership(guild.guild_id, toUserWireId(member.id));

    const request = new NextRequest("http://internal/x", { method: "DELETE", headers: SECRET_HEADERS });
    const response = await DELETE(request, {
      params: Promise.resolve({ guildId: guild.guild_id, userId: toUserWireId(member.id) })
    });
    expect(response.status).toBe(200);
  });

  it("returns 404 when the membership doesn't exist", async () => {
    const request = new NextRequest("http://internal/x", { method: "DELETE", headers: SECRET_HEADERS });
    const response = await DELETE(request, {
      params: Promise.resolve({
        guildId: "g_00000000-0000-0000-0000-000000000000",
        userId: "u_00000000-0000-0000-0000-000000000000"
      })
    });
    expect(response.status).toBe(404);
  });
});

describe("PATCH /internal/guild-memberships/:guildId/:userId", () => {
  it("returns 401 without the shared secret", async () => {
    const request = new NextRequest("http://internal/x", { method: "PATCH", body: "{}" });
    const response = await PATCH(request, { params: Promise.resolve({ guildId: "g_x", userId: "u_x" }) });
    expect(response.status).toBe(401);
  });

  it("returns 400 for a negative role_rank", async () => {
    const request = new NextRequest("http://internal/x", {
      method: "PATCH",
      headers: SECRET_HEADERS,
      body: JSON.stringify({ role_rank: -1 })
    });
    const response = await PATCH(request, { params: Promise.resolve({ guildId: "g_x", userId: "u_x" }) });
    expect(response.status).toBe(400);
  });

  it("updates role_rank and returns the resolved role_label", async () => {
    const [owner] = await db
      .insert(users)
      .values({ discordId: "20", discordUsername: "frank" })
      .returning();
    const [member] = await db
      .insert(users)
      .values({ discordId: "21", discordUsername: "grace" })
      .returning();
    const guild = await createGuild("My Guild", toUserWireId(owner.id));
    await createMembership(guild.guild_id, toUserWireId(member.id));

    const request = new NextRequest("http://internal/x", {
      method: "PATCH",
      headers: SECRET_HEADERS,
      body: JSON.stringify({ role_rank: kOfficerRank })
    });
    const response = await PATCH(request, {
      params: Promise.resolve({ guildId: guild.guild_id, userId: toUserWireId(member.id) })
    });
    expect(response.status).toBe(200);
    const body = await response.json();
    expect(body).toEqual({ role_rank: kOfficerRank, role_label: "Officer" });
  });

  it("returns 404 for a membership that doesn't exist", async () => {
    const request = new NextRequest("http://internal/x", {
      method: "PATCH",
      headers: SECRET_HEADERS,
      body: JSON.stringify({ role_rank: kOfficerRank })
    });
    const response = await PATCH(request, {
      params: Promise.resolve({
        guildId: "g_00000000-0000-0000-0000-000000000000",
        userId: "u_00000000-0000-0000-0000-000000000000"
      })
    });
    expect(response.status).toBe(404);
  });
});
