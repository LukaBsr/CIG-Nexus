import { sql } from "drizzle-orm";
import { NextRequest } from "next/server";
import { afterEach, beforeAll, describe, expect, it } from "vitest";

import { db } from "@/db/client";
import { channels, guildInvites, guildJoinRequests, guildMemberships, guilds, users } from "@/db/schema";
import { createGuild } from "@/lib/internal/catalog";
import { createInvite } from "@/lib/internal/invites";
import { toUserWireId } from "@/lib/internal/wireIds";

import { POST } from "./route";

const SECRET_HEADERS = { "x-internal-secret": "test-internal-secret", "content-type": "application/json" };

beforeAll(() => {
  process.env.INTERNAL_API_SHARED_SECRET = "test-internal-secret";
});

afterEach(async () => {
  await db.execute(
    sql`TRUNCATE TABLE ${channels}, ${guildInvites}, ${guildJoinRequests}, ${guildMemberships}, ${guilds}, ${users} RESTART IDENTITY CASCADE`
  );
});

describe("POST /internal/guild-invites/:code/redeem", () => {
  it("returns 401 without the shared secret", async () => {
    const request = new NextRequest("http://internal/x", { method: "POST", body: "{}" });
    const response = await POST(request, { params: Promise.resolve({ code: "abc" }) });
    expect(response.status).toBe(401);
  });

  it("returns 400 without user_id", async () => {
    const request = new NextRequest("http://internal/x", {
      method: "POST",
      headers: SECRET_HEADERS,
      body: JSON.stringify({})
    });
    const response = await POST(request, { params: Promise.resolve({ code: "abc" }) });
    expect(response.status).toBe(400);
  });

  it("redeems successfully and returns a discriminated ok:true body", async () => {
    const [owner] = await db
      .insert(users)
      .values({ discordId: "1", discordUsername: "alice" })
      .returning();
    const [redeemer] = await db
      .insert(users)
      .values({ discordId: "2", discordUsername: "bob" })
      .returning();
    const guild = await createGuild("My Guild", toUserWireId(owner.id));
    const invite = await createInvite(guild.guild_id, toUserWireId(owner.id), null, null);

    const request = new NextRequest("http://internal/x", {
      method: "POST",
      headers: SECRET_HEADERS,
      body: JSON.stringify({ user_id: toUserWireId(redeemer.id) })
    });
    const response = await POST(request, { params: Promise.resolve({ code: invite.code }) });
    expect(response.status).toBe(200);
    const body = await response.json();
    expect(body).toEqual({ ok: true, kind: "member", guild_id: guild.guild_id, role_rank: 0 });
  });

  it("returns 200 with ok:false for an unknown code (a domain result, not an HTTP error)", async () => {
    const [redeemer] = await db
      .insert(users)
      .values({ discordId: "3", discordUsername: "carol" })
      .returning();
    const request = new NextRequest("http://internal/x", {
      method: "POST",
      headers: SECRET_HEADERS,
      body: JSON.stringify({ user_id: toUserWireId(redeemer.id) })
    });
    const response = await POST(request, { params: Promise.resolve({ code: "nonexistent" }) });
    expect(response.status).toBe(200);
    const body = await response.json();
    expect(body).toEqual({ ok: false, error: "not_found" });
  });

  it("returns 400 for an invalid user_id", async () => {
    const request = new NextRequest("http://internal/x", {
      method: "POST",
      headers: SECRET_HEADERS,
      body: JSON.stringify({ user_id: "not-a-wire-id" })
    });
    const response = await POST(request, { params: Promise.resolve({ code: "abc" }) });
    expect(response.status).toBe(400);
  });
});
