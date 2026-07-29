import { sql } from "drizzle-orm";
import { NextRequest } from "next/server";
import { afterEach, beforeAll, describe, expect, it } from "vitest";

import { db } from "@/db/client";
import { guilds, users } from "@/db/schema";
import { toUserWireId } from "@/lib/internal/wireIds";

import { POST } from "./route";

const URL = "http://internal/internal/guilds";
const SECRET_HEADERS = { "x-internal-secret": "test-internal-secret", "content-type": "application/json" };

beforeAll(() => {
  process.env.INTERNAL_API_SHARED_SECRET = "test-internal-secret";
});

afterEach(async () => {
  await db.execute(sql`TRUNCATE TABLE ${guilds}, ${users} RESTART IDENTITY CASCADE`);
});

describe("POST /internal/guilds", () => {
  it("returns 401 without the shared secret", async () => {
    const request = new NextRequest(URL, { method: "POST", body: "{}" });
    expect((await POST(request)).status).toBe(401);
  });

  it("creates a guild for a valid owner_id", async () => {
    const [owner] = await db
      .insert(users)
      .values({ discordId: "1", discordUsername: "alice" })
      .returning();

    const request = new NextRequest(URL, {
      method: "POST",
      headers: SECRET_HEADERS,
      body: JSON.stringify({ name: "My Guild", owner_id: toUserWireId(owner.id) })
    });
    const response = await POST(request);
    expect(response.status).toBe(201);

    const body = (await response.json()) as { guild_id: string; name: string; owner_id: string };
    expect(body.name).toBe("My Guild");
    expect(body.owner_id).toBe(toUserWireId(owner.id));
  });

  it("returns 400 for a missing name", async () => {
    const request = new NextRequest(URL, {
      method: "POST",
      headers: SECRET_HEADERS,
      body: JSON.stringify({ owner_id: "u_x" })
    });
    expect((await POST(request)).status).toBe(400);
  });

  it("returns 400 for an owner_id that doesn't reference a real user", async () => {
    const request = new NextRequest(URL, {
      method: "POST",
      headers: SECRET_HEADERS,
      body: JSON.stringify({ name: "My Guild", owner_id: "u_00000000-0000-0000-0000-000000000000" })
    });
    expect((await POST(request)).status).toBe(400);
  });

  it("returns 400 for a name over 64 characters", async () => {
    const [owner] = await db
      .insert(users)
      .values({ discordId: "2", discordUsername: "bob" })
      .returning();
    const request = new NextRequest(URL, {
      method: "POST",
      headers: SECRET_HEADERS,
      body: JSON.stringify({ name: "x".repeat(65), owner_id: toUserWireId(owner.id) })
    });
    expect((await POST(request)).status).toBe(400);
  });
});
