import { sql } from "drizzle-orm";
import { NextRequest } from "next/server";
import { afterEach, beforeAll, describe, expect, it } from "vitest";

import { db } from "@/db/client";
import { channels, guilds, users } from "@/db/schema";
import { createGuild } from "@/lib/internal/catalog";
import { toUserWireId } from "@/lib/internal/wireIds";

import { POST } from "./route";

const URL = "http://internal/api/internal/channels";
const SECRET_HEADERS = { "x-internal-secret": "test-internal-secret", "content-type": "application/json" };

beforeAll(() => {
  process.env.INTERNAL_API_SHARED_SECRET = "test-internal-secret";
});

afterEach(async () => {
  await db.execute(sql`TRUNCATE TABLE ${channels}, ${guilds}, ${users} RESTART IDENTITY CASCADE`);
});

describe("POST /api/internal/channels", () => {
  it("returns 401 without the shared secret", async () => {
    const request = new NextRequest(URL, { method: "POST", body: "{}" });
    expect((await POST(request)).status).toBe(401);
  });

  it("creates a TEXT channel", async () => {
    const [owner] = await db
      .insert(users)
      .values({ discordId: "1", discordUsername: "alice" })
      .returning();
    const guild = await createGuild("My Guild", toUserWireId(owner.id));

    const request = new NextRequest(URL, {
      method: "POST",
      headers: SECRET_HEADERS,
      body: JSON.stringify({ guild_id: guild.guild_id, name: "general", channel_type: "TEXT" })
    });
    const response = await POST(request);
    expect(response.status).toBe(201);
    const body = (await response.json()) as { channel_type: string };
    expect(body.channel_type).toBe("TEXT");
  });

  it("returns 400 for an invalid channel_type", async () => {
    const request = new NextRequest(URL, {
      method: "POST",
      headers: SECRET_HEADERS,
      body: JSON.stringify({ guild_id: "g_x", name: "general", channel_type: "AUDIO" })
    });
    expect((await POST(request)).status).toBe(400);
  });

  it("returns 400 for a guild_id that doesn't reference a real guild", async () => {
    const request = new NextRequest(URL, {
      method: "POST",
      headers: SECRET_HEADERS,
      body: JSON.stringify({
        guild_id: "g_00000000-0000-0000-0000-000000000000",
        name: "general",
        channel_type: "TEXT"
      })
    });
    expect((await POST(request)).status).toBe(400);
  });
});
