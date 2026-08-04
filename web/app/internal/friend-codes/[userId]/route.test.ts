import { sql } from "drizzle-orm";
import { NextRequest } from "next/server";
import { afterEach, beforeAll, describe, expect, it } from "vitest";

import { db } from "@/db/client";
import { users } from "@/db/schema";
import { toUserWireId } from "@/lib/internal/wireIds";

import { GET } from "./route";

const SECRET_HEADERS = { "x-internal-secret": "test-internal-secret" };

beforeAll(() => {
  process.env.INTERNAL_API_SHARED_SECRET = "test-internal-secret";
});

afterEach(async () => {
  await db.execute(sql`TRUNCATE TABLE ${users} RESTART IDENTITY CASCADE`);
});

describe("GET /internal/friend-codes/:userId", () => {
  it("returns 401 without the shared secret", async () => {
    const request = new NextRequest("http://internal/x");
    const response = await GET(request, { params: Promise.resolve({ userId: "u_x" }) });
    expect(response.status).toBe(401);
  });

  it("returns the user's stored code", async () => {
    const [a] = await db.insert(users).values({ discordId: "1", discordUsername: "alice", friendCode: "fixed-code" }).returning();

    const request = new NextRequest("http://internal/x", { headers: SECRET_HEADERS });
    const response = await GET(request, { params: Promise.resolve({ userId: toUserWireId(a.id) }) });
    expect(response.status).toBe(200);
    expect(await response.json()).toEqual({ code: "fixed-code" });
  });

  it("returns 400 for a malformed user id", async () => {
    const request = new NextRequest("http://internal/x", { headers: SECRET_HEADERS });
    const response = await GET(request, { params: Promise.resolve({ userId: "not-a-wire-id" }) });
    expect(response.status).toBe(400);
  });
});
