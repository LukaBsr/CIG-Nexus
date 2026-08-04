import { sql } from "drizzle-orm";
import { NextRequest } from "next/server";
import { afterEach, beforeAll, describe, expect, it } from "vitest";

import { db } from "@/db/client";
import { guildMemberships, guilds, users } from "@/db/schema";
import { createGuild, createMembership } from "@/lib/internal/catalog";
import { kOfficerRank, kOwnerRank } from "@/lib/internal/roleThemes";
import { toUserWireId } from "@/lib/internal/wireIds";

import { GET, POST } from "./route";

const URL = "http://internal/internal/guild-memberships";
const SECRET_HEADERS = { "x-internal-secret": "test-internal-secret", "content-type": "application/json" };

beforeAll(() => {
  process.env.INTERNAL_API_SHARED_SECRET = "test-internal-secret";
});

afterEach(async () => {
  await db.execute(sql`TRUNCATE TABLE ${guildMemberships}, ${guilds}, ${users} RESTART IDENTITY CASCADE`);
});

describe("POST /internal/guild-memberships", () => {
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

  it("is idempotent for a duplicate membership (a reconnecting client's SessionManager starts empty, §8.1)", async () => {
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
    const response = await POST(request);
    expect(response.status).toBe(201);

    const memberships = await db
      .select()
      .from(guildMemberships)
      .where(sql`${guildMemberships.guildId} = ${guild.guild_id.slice(2)}`);
    expect(memberships).toHaveLength(1);
  });

  it("returns 400 for a negative role_rank", async () => {
    const request = new NextRequest(URL, {
      method: "POST",
      headers: SECRET_HEADERS,
      body: JSON.stringify({ guild_id: "g_x", user_id: "u_x", role_rank: -1 })
    });
    expect((await POST(request)).status).toBe(400);
  });
});

describe("GET /internal/guild-memberships", () => {
  it("returns 401 without the shared secret", async () => {
    const request = new NextRequest(`${URL}?guild_id=g_x`, { method: "GET" });
    expect((await GET(request)).status).toBe(401);
  });

  it("returns 400 without guild_id", async () => {
    const request = new NextRequest(URL, { method: "GET", headers: SECRET_HEADERS });
    expect((await GET(request)).status).toBe(400);
  });

  it("returns 404 for a guild that doesn't exist", async () => {
    const request = new NextRequest(
      `${URL}?guild_id=g_00000000-0000-0000-0000-000000000000`,
      { method: "GET", headers: SECRET_HEADERS }
    );
    expect((await GET(request)).status).toBe(404);
  });

  it("returns the roster with resolved role_label", async () => {
    const [owner] = await db.insert(users).values({ discordId: "5", discordUsername: "dave" }).returning();
    const [member] = await db.insert(users).values({ discordId: "6", discordUsername: "erin" }).returning();
    const guild = await createGuild("My Guild", toUserWireId(owner.id));
    await createMembership(guild.guild_id, toUserWireId(member.id), kOfficerRank);

    const request = new NextRequest(`${URL}?guild_id=${guild.guild_id}`, {
      method: "GET",
      headers: SECRET_HEADERS
    });
    const response = await GET(request);
    expect(response.status).toBe(200);
    const body = (await response.json()) as { members: Array<{ user_id: string; role_rank: number; role_label: string }> };
    expect(body.members).toHaveLength(2);
    const ownerRow = body.members.find((m) => m.user_id === toUserWireId(owner.id));
    expect(ownerRow?.role_rank).toBe(kOwnerRank);
    expect(ownerRow?.role_label).toBe("Captain");
    const memberRow = body.members.find((m) => m.user_id === toUserWireId(member.id));
    expect(memberRow?.role_label).toBe("Officer");
  });

  // docs/social/friends-dms-design.md §3.3: the ?user_id= variant used by
  // canSendDm()'s disconnected-peer fallback.
  it("returns a user's guild ids via ?user_id=", async () => {
    const [owner] = await db.insert(users).values({ discordId: "7", discordUsername: "frank" }).returning();
    const guildA = await createGuild("Guild A", toUserWireId(owner.id));
    const guildB = await createGuild("Guild B", toUserWireId(owner.id));

    const request = new NextRequest(`${URL}?user_id=${toUserWireId(owner.id)}`, {
      method: "GET",
      headers: SECRET_HEADERS
    });
    const response = await GET(request);
    expect(response.status).toBe(200);
    const body = (await response.json()) as { guild_ids: string[] };
    expect(body.guild_ids.sort()).toEqual([guildA.guild_id, guildB.guild_id].sort());
  });

  it("returns an empty array (not 404) for a user with no guilds", async () => {
    const [owner] = await db.insert(users).values({ discordId: "8", discordUsername: "gail" }).returning();
    const request = new NextRequest(`${URL}?user_id=${toUserWireId(owner.id)}`, {
      method: "GET",
      headers: SECRET_HEADERS
    });
    const response = await GET(request);
    expect(response.status).toBe(200);
    expect((await response.json()) as { guild_ids: string[] }).toEqual({ guild_ids: [] });
  });
});
