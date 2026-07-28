import { sql } from "drizzle-orm";
import { NextRequest } from "next/server";
import { afterEach, beforeAll, describe, expect, it } from "vitest";

import { db } from "@/db/client";
import { guilds, users } from "@/db/schema";
import { createGuild } from "@/lib/internal/catalog";
import { toUserWireId } from "@/lib/internal/wireIds";

import { DELETE } from "./route";

const SECRET_HEADERS = { "x-internal-secret": "test-internal-secret" };

beforeAll(() => {
  process.env.INTERNAL_API_SHARED_SECRET = "test-internal-secret";
});

afterEach(async () => {
  await db.execute(sql`TRUNCATE TABLE ${guilds}, ${users} RESTART IDENTITY CASCADE`);
});

describe("DELETE /api/internal/guilds/:id", () => {
  it("returns 401 without the shared secret", async () => {
    const request = new NextRequest("http://internal/api/internal/guilds/g_x", { method: "DELETE" });
    const response = await DELETE(request, { params: Promise.resolve({ id: "g_x" }) });
    expect(response.status).toBe(401);
  });

  it("deletes an existing guild", async () => {
    const [owner] = await db
      .insert(users)
      .values({ discordId: "1", discordUsername: "alice" })
      .returning();
    const guild = await createGuild("My Guild", toUserWireId(owner.id));

    const request = new NextRequest(`http://internal/api/internal/guilds/${guild.guild_id}`, {
      method: "DELETE",
      headers: SECRET_HEADERS
    });
    const response = await DELETE(request, { params: Promise.resolve({ id: guild.guild_id }) });
    expect(response.status).toBe(200);
  });

  it("returns 404 for an unknown guild", async () => {
    const request = new NextRequest("http://internal/api/internal/guilds/g_unknown", {
      method: "DELETE",
      headers: SECRET_HEADERS
    });
    const response = await DELETE(request, { params: Promise.resolve({ id: "g_unknown" }) });
    expect(response.status).toBe(404);
  });
});
