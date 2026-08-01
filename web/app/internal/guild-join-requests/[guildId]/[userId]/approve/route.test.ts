import { sql } from "drizzle-orm";
import { NextRequest } from "next/server";
import { afterEach, beforeAll, describe, expect, it } from "vitest";

import { db } from "@/db/client";
import { channels, guildInvites, guildJoinRequests, guildMemberships, guilds, users } from "@/db/schema";
import { createGuild } from "@/lib/internal/catalog";
import { createJoinRequest } from "@/lib/internal/joinRequests";
import { toUserWireId } from "@/lib/internal/wireIds";

import { POST } from "./route";

const SECRET_HEADERS = { "x-internal-secret": "test-internal-secret" };

beforeAll(() => {
  process.env.INTERNAL_API_SHARED_SECRET = "test-internal-secret";
});

afterEach(async () => {
  await db.execute(
    sql`TRUNCATE TABLE ${channels}, ${guildInvites}, ${guildJoinRequests}, ${guildMemberships}, ${guilds}, ${users} RESTART IDENTITY CASCADE`
  );
});

describe("POST /internal/guild-join-requests/:guildId/:userId/approve", () => {
  it("returns 401 without the shared secret", async () => {
    const request = new NextRequest("http://internal/x", { method: "POST" });
    const response = await POST(request, { params: Promise.resolve({ guildId: "g_x", userId: "u_x" }) });
    expect(response.status).toBe(401);
  });

  it("approves an existing request", async () => {
    const [owner] = await db
      .insert(users)
      .values({ discordId: "1", discordUsername: "alice" })
      .returning();
    const [requester] = await db
      .insert(users)
      .values({ discordId: "2", discordUsername: "bob" })
      .returning();
    const guild = await createGuild("My Guild", toUserWireId(owner.id), "application");
    await createJoinRequest(guild.guild_id, toUserWireId(requester.id));

    const request = new NextRequest("http://internal/x", { method: "POST", headers: SECRET_HEADERS });
    const response = await POST(request, {
      params: Promise.resolve({ guildId: guild.guild_id, userId: toUserWireId(requester.id) })
    });
    expect(response.status).toBe(200);
    const body = await response.json();
    expect(body).toEqual({ role_rank: 0 });
  });

  it("returns 404 when the request doesn't exist", async () => {
    const request = new NextRequest("http://internal/x", { method: "POST", headers: SECRET_HEADERS });
    const response = await POST(request, {
      params: Promise.resolve({
        guildId: "g_00000000-0000-0000-0000-000000000000",
        userId: "u_00000000-0000-0000-0000-000000000000"
      })
    });
    expect(response.status).toBe(404);
  });
});
