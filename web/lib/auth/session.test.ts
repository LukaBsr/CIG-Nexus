import { sql } from "drizzle-orm";
import { afterEach, describe, expect, it } from "vitest";

import { db } from "@/db/client";
import { sessions, users } from "@/db/schema";

import {
  createSession,
  findActiveSessionByRefreshToken,
  hashRefreshToken,
  revokeSession
} from "./session";

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

describe("createSession", () => {
  it("persists only the hash of the refresh token, not the raw value", async () => {
    const user = await insertUser("1");
    const { session, refreshToken } = await createSession(user.id, {});

    expect(session.refreshTokenHash).toBe(hashRefreshToken(refreshToken));
    expect(session.refreshTokenHash).not.toBe(refreshToken);
  });

  it("sets expiry roughly 30 days out", async () => {
    const user = await insertUser("2");
    const before = Date.now();
    const { session } = await createSession(user.id, {});
    const ttlDays = (session.expiresAt.getTime() - before) / (24 * 60 * 60 * 1000);
    expect(ttlDays).toBeCloseTo(30, 0);
  });
});

describe("findActiveSessionByRefreshToken", () => {
  it("finds a session by its raw refresh token", async () => {
    const user = await insertUser("3");
    const { session, refreshToken } = await createSession(user.id, {});
    const found = await findActiveSessionByRefreshToken(refreshToken);
    expect(found?.id).toBe(session.id);
  });

  it("returns null for an unknown token", async () => {
    await expect(findActiveSessionByRefreshToken("not-a-real-token")).resolves.toBeNull();
  });

  it("returns null for a revoked session", async () => {
    const user = await insertUser("4");
    const { session, refreshToken } = await createSession(user.id, {});
    await revokeSession(session.id);
    await expect(findActiveSessionByRefreshToken(refreshToken)).resolves.toBeNull();
  });

  it("returns null for an expired session", async () => {
    const user = await insertUser("5");
    const { refreshToken } = await createSession(user.id, {});
    await db
      .update(sessions)
      .set({ expiresAt: new Date(Date.now() - 1000) })
      .where(sql`${sessions.userId} = ${user.id}`);
    await expect(findActiveSessionByRefreshToken(refreshToken)).resolves.toBeNull();
  });
});

describe("revokeSession", () => {
  it("sets revoked_at", async () => {
    const user = await insertUser("6");
    const { session } = await createSession(user.id, {});
    await revokeSession(session.id);

    const [reloaded] = await db.select().from(sessions).where(sql`${sessions.id} = ${session.id}`);
    expect(reloaded.revokedAt).not.toBeNull();
  });
});
