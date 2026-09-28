import { sql } from "drizzle-orm";
import { afterEach, describe, expect, it } from "vitest";

import { db } from "@/db/client";
import { sessions, users } from "@/db/schema";

import { getRevokedSessionIds } from "./revokedSessions";

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

describe("getRevokedSessionIds", () => {
  it("returns only sessions revoked after the given timestamp", async () => {
    const user = await insertUser("7");
    const [oldRevoked] = await db
      .insert(sessions)
      .values({
        userId: user.id,
        expiresAt: new Date(Date.now() + 60_000),
        refreshTokenHash: "hash-old",
        revokedAt: new Date(Date.now() - 60_000)
      })
      .returning();
    const cutoff = new Date();
    const [newRevoked] = await db
      .insert(sessions)
      .values({
        userId: user.id,
        expiresAt: new Date(Date.now() + 60_000),
        refreshTokenHash: "hash-new",
        revokedAt: new Date(Date.now() + 1000)
      })
      .returning();
    await db.insert(sessions).values({
      userId: user.id,
      expiresAt: new Date(Date.now() + 60_000),
      refreshTokenHash: "hash-active"
    });

    const revoked = await getRevokedSessionIds(cutoff);
    expect(revoked).toEqual([newRevoked.id]);
    expect(revoked).not.toContain(oldRevoked.id);
  });
});
