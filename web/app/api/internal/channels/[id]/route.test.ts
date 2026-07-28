import { sql } from "drizzle-orm";
import { NextRequest } from "next/server";
import { afterEach, beforeAll, describe, expect, it } from "vitest";

import { db } from "@/db/client";
import { channels, guilds, users } from "@/db/schema";
import { createChannel, createGuild } from "@/lib/internal/catalog";
import { toUserWireId } from "@/lib/internal/wireIds";

import { DELETE } from "./route";

const SECRET_HEADERS = { "x-internal-secret": "test-internal-secret" };

beforeAll(() => {
  process.env.INTERNAL_API_SHARED_SECRET = "test-internal-secret";
});

afterEach(async () => {
  await db.execute(sql`TRUNCATE TABLE ${channels}, ${guilds}, ${users} RESTART IDENTITY CASCADE`);
});

describe("DELETE /api/internal/channels/:id", () => {
  it("returns 401 without the shared secret", async () => {
    const request = new NextRequest("http://internal/x", { method: "DELETE" });
    const response = await DELETE(request, { params: Promise.resolve({ id: "c_x" }) });
    expect(response.status).toBe(401);
  });

  it("deletes an existing channel", async () => {
    const [owner] = await db
      .insert(users)
      .values({ discordId: "1", discordUsername: "alice" })
      .returning();
    const guild = await createGuild("My Guild", toUserWireId(owner.id));
    const channel = await createChannel(guild.guild_id, "general", "TEXT");

    const request = new NextRequest("http://internal/x", { method: "DELETE", headers: SECRET_HEADERS });
    const response = await DELETE(request, { params: Promise.resolve({ id: channel.channel_id }) });
    expect(response.status).toBe(200);
  });

  it("returns 404 for an unknown channel", async () => {
    const request = new NextRequest("http://internal/x", { method: "DELETE", headers: SECRET_HEADERS });
    const response = await DELETE(request, {
      params: Promise.resolve({ id: "c_00000000-0000-0000-0000-000000000000" })
    });
    expect(response.status).toBe(404);
  });
});
