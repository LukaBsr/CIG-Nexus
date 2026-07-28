import { sql } from "drizzle-orm";
import { importSPKI, jwtVerify } from "jose";
import { NextRequest } from "next/server";
import { afterEach, beforeAll, describe, expect, it } from "vitest";

import { db } from "@/db/client";
import { sessions, users } from "@/db/schema";
import { createSession } from "@/lib/auth/session";

import { generateTestRsaKeyPair } from "../../../../test/rsaKeys";
import { GET } from "./route";

let publicKeyPem: string;

beforeAll(() => {
  const { privateKeyPem, publicKeyPem: pub } = generateTestRsaKeyPair();
  process.env.SESSION_JWT_PRIVATE_KEY = privateKeyPem;
  publicKeyPem = pub;
});

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

describe("GET /api/auth/session-token", () => {
  it("returns 401 when there is no refresh cookie", async () => {
    const request = new NextRequest("http://localhost:3000/api/auth/session-token");
    const response = await GET(request);
    expect(response.status).toBe(401);
  });

  it("returns 401 for an unknown refresh token", async () => {
    const request = new NextRequest("http://localhost:3000/api/auth/session-token", {
      headers: new Headers({ cookie: "__session=not-a-real-token" })
    });
    const response = await GET(request);
    expect(response.status).toBe(401);
  });

  it("returns a JWT identifying the session's user for a valid refresh cookie", async () => {
    const user = await insertUser("77");
    const { refreshToken } = await createSession(user.id, {});

    const request = new NextRequest("http://localhost:3000/api/auth/session-token", {
      headers: new Headers({ cookie: `__session=${refreshToken}` })
    });
    const response = await GET(request);
    expect(response.status).toBe(200);

    const body = (await response.json()) as { token: string; expires_at: string };
    const publicKey = await importSPKI(publicKeyPem, "RS256");
    const { payload } = await jwtVerify(body.token, publicKey, { algorithms: ["RS256"] });

    expect(payload.sub).toBe(`u_${user.id}`);
    expect(payload.discord_id).toBe("77");
  });
});
