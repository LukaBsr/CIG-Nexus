import { sql } from "drizzle-orm";
import { NextRequest } from "next/server";
import { afterEach, beforeAll, describe, expect, it } from "vitest";

import { db } from "@/db/client";
import { channels, guildInvites, guildJoinRequests, guildMemberships, guilds, users } from "@/db/schema";
import { createGuild } from "@/lib/internal/catalog";
import { createInvite } from "@/lib/internal/invites";
import { toUserWireId } from "@/lib/internal/wireIds";

import { DELETE } from "./route";

const SECRET_HEADERS = { "x-internal-secret": "test-internal-secret" };

beforeAll(() => {
  process.env.INTERNAL_API_SHARED_SECRET = "test-internal-secret";
});

afterEach(async () => {
  await db.execute(
    sql`TRUNCATE TABLE ${channels}, ${guildInvites}, ${guildJoinRequests}, ${guildMemberships}, ${guilds}, ${users} RESTART IDENTITY CASCADE`
  );
});

describe("DELETE /internal/guild-invites/:code", () => {
  it("returns 401 without the shared secret", async () => {
    const request = new NextRequest("http://internal/x", { method: "DELETE" });
    const response = await DELETE(request, { params: Promise.resolve({ code: "abc" }) });
    expect(response.status).toBe(401);
  });

  it("returns 400 without guild_id", async () => {
    const request = new NextRequest("http://internal/x", { method: "DELETE", headers: SECRET_HEADERS });
    const response = await DELETE(request, { params: Promise.resolve({ code: "abc" }) });
    expect(response.status).toBe(400);
  });

  it("revokes an existing invite", async () => {
    const [owner] = await db
      .insert(users)
      .values({ discordId: "1", discordUsername: "alice" })
      .returning();
    const guild = await createGuild("My Guild", toUserWireId(owner.id));
    const invite = await createInvite(guild.guild_id, toUserWireId(owner.id), null, null);

    const request = new NextRequest(`http://internal/x?guild_id=${guild.guild_id}`, {
      method: "DELETE",
      headers: SECRET_HEADERS
    });
    const response = await DELETE(request, { params: Promise.resolve({ code: invite.code }) });
    expect(response.status).toBe(200);
  });

  it("returns 404 for an unknown code", async () => {
    const [owner] = await db
      .insert(users)
      .values({ discordId: "2", discordUsername: "bob" })
      .returning();
    const guild = await createGuild("My Guild", toUserWireId(owner.id));

    const request = new NextRequest(`http://internal/x?guild_id=${guild.guild_id}`, {
      method: "DELETE",
      headers: SECRET_HEADERS
    });
    const response = await DELETE(request, { params: Promise.resolve({ code: "nonexistent" }) });
    expect(response.status).toBe(404);
  });
});
