import { sql } from "drizzle-orm";
import { NextRequest } from "next/server";
import { afterEach, describe, expect, it } from "vitest";

import { db } from "@/db/client";
import { sessions, users } from "@/db/schema";
import { createSession, findActiveSessionByRefreshToken } from "@/lib/auth/session";

import { POST } from "./route";

afterEach(async () => {
  await db.execute(sql`TRUNCATE TABLE ${sessions}, ${users} RESTART IDENTITY CASCADE`);
});

async function insertUser(discordId: string) {
  const [user] = await db
    .insert(users)
    .values({ discordId, discordUsername: `user_${discordId}` })
    .returning();
  return user;
}

describe("POST /api/auth/logout", () => {
  it("revokes the session referenced by the refresh cookie", async () => {
    const user = await insertUser("1");
    const { refreshToken } = await createSession(user.id, {});

    const request = new NextRequest("http://localhost:3000/api/auth/logout", {
      method: "POST",
      headers: new Headers({ cookie: `__session=${refreshToken}` })
    });

    const response = await POST(request);

    expect(response.status).toBe(200);
    await expect(findActiveSessionByRefreshToken(refreshToken)).resolves.toBeNull();
  });

  it("clears the refresh cookie", async () => {
    const request = new NextRequest("http://localhost:3000/api/auth/logout", { method: "POST" });
    const response = await POST(request);
    const setCookie = response.headers.get("set-cookie");
    expect(setCookie).toContain("__session=;");
  });

  it("is a no-op (not an error) when there is no session cookie", async () => {
    const request = new NextRequest("http://localhost:3000/api/auth/logout", { method: "POST" });
    const response = await POST(request);
    expect(response.status).toBe(200);
  });
});
