import { sql } from "drizzle-orm";
import { NextRequest } from "next/server";
import { afterEach, beforeAll, describe, expect, it } from "vitest";

import { db } from "@/db/client";
import { guilds, users } from "@/db/schema";
import { createGuild } from "@/lib/internal/catalog";
import { toUserWireId } from "@/lib/internal/wireIds";

import { GET } from "./route";

const URL = "http://internal/internal/catalog";

beforeAll(() => {
  process.env.INTERNAL_API_SHARED_SECRET = "test-internal-secret";
});

afterEach(async () => {
  await db.execute(sql`TRUNCATE TABLE ${guilds}, ${users} RESTART IDENTITY CASCADE`);
});

describe("GET /internal/catalog", () => {
  it("returns 401 without the shared secret header", async () => {
    const response = await GET(new NextRequest(URL));
    expect(response.status).toBe(401);
  });

  it("returns 401 with the wrong secret", async () => {
    const request = new NextRequest(URL, { headers: { "x-internal-secret": "wrong" } });
    expect((await GET(request)).status).toBe(401);
  });

  it("returns the current catalog with the correct secret", async () => {
    const [user] = await db
      .insert(users)
      .values({ discordId: "1", discordUsername: "alice" })
      .returning();
    await createGuild("My Guild", toUserWireId(user.id));

    const request = new NextRequest(URL, { headers: { "x-internal-secret": "test-internal-secret" } });
    const response = await GET(request);
    expect(response.status).toBe(200);

    const body = (await response.json()) as { guilds: unknown[]; memberships: unknown[] };
    expect(body.guilds).toHaveLength(1);
    expect(body.memberships).toHaveLength(1);
  });
});
