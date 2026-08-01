import { sql } from "drizzle-orm";
import { NextRequest } from "next/server";
import { afterEach, beforeAll, describe, expect, it } from "vitest";

import { db } from "@/db/client";
import { guilds, users } from "@/db/schema";
import { createGuild } from "@/lib/internal/catalog";
import { toUserWireId } from "@/lib/internal/wireIds";

import { POST } from "./route";

const SECRET_HEADERS = { "x-internal-secret": "test-internal-secret", "content-type": "application/json" };

beforeAll(() => {
  process.env.INTERNAL_API_SHARED_SECRET = "test-internal-secret";
});

afterEach(async () => {
  await db.execute(sql`TRUNCATE TABLE ${guilds}, ${users} RESTART IDENTITY CASCADE`);
});

describe("POST /internal/guilds/:id/visibility", () => {
  it("returns 401 without the shared secret", async () => {
    const request = new NextRequest("http://internal/x", { method: "POST", body: "{}" });
    const response = await POST(request, { params: Promise.resolve({ id: "g_x" }) });
    expect(response.status).toBe(401);
  });

  it("updates visibility", async () => {
    const [owner] = await db
      .insert(users)
      .values({ discordId: "1", discordUsername: "alice" })
      .returning();
    const guild = await createGuild("My Guild", toUserWireId(owner.id));

    const request = new NextRequest("http://internal/x", {
      method: "POST",
      headers: SECRET_HEADERS,
      body: JSON.stringify({ visibility: "private" })
    });
    const response = await POST(request, { params: Promise.resolve({ id: guild.guild_id }) });
    expect(response.status).toBe(200);
    const body = await response.json();
    expect(body).toEqual({ visibility: "private" });
  });

  it("returns 400 for an invalid visibility", async () => {
    const request = new NextRequest("http://internal/x", {
      method: "POST",
      headers: SECRET_HEADERS,
      body: JSON.stringify({ visibility: "secret" })
    });
    const response = await POST(request, { params: Promise.resolve({ id: "g_x" }) });
    expect(response.status).toBe(400);
  });

  it("returns 404 for a guild that doesn't exist", async () => {
    const request = new NextRequest("http://internal/x", {
      method: "POST",
      headers: SECRET_HEADERS,
      body: JSON.stringify({ visibility: "private" })
    });
    const response = await POST(request, {
      params: Promise.resolve({ id: "g_00000000-0000-0000-0000-000000000000" })
    });
    expect(response.status).toBe(404);
  });
});
