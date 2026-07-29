import { sql } from "drizzle-orm";
import { afterEach, describe, expect, it } from "vitest";

import { db } from "../client";
import { channels } from "./channels";
import { guildMemberships } from "./guildMemberships";
import { guilds } from "./guilds";
import { sessions } from "./sessions";
import { users } from "./users";

afterEach(async () => {
  await db.execute(
    sql`TRUNCATE TABLE ${channels}, ${guildMemberships}, ${guilds}, ${sessions}, ${users} RESTART IDENTITY CASCADE`
  );
});

async function insertUser(discordId: string) {
  const [user] = await db
    .insert(users)
    .values({
      discordId,
      discordUsername: `user_${discordId}`
    })
    .returning();
  return user;
}

// Drizzle wraps the underlying pg error in a DrizzleQueryError whose own
// .message is just "Failed query: ..." — the real Postgres error text (the
// constraint name, "duplicate key value", etc.) lives on .cause.
async function expectPgError(promise: Promise<unknown>, pattern: RegExp) {
  try {
    await promise;
    expect.fail("expected the query to reject");
  } catch (err) {
    const cause = (err as { cause?: unknown }).cause;
    const message = cause instanceof Error ? cause.message : (err as Error).message;
    expect(message).toMatch(pattern);
  }
}

describe("users", () => {
  it("rejects a duplicate discord_id", async () => {
    await insertUser("111");
    await expectPgError(insertUser("111"), /duplicate key value/i);
  });
});

describe("guilds", () => {
  it("rejects a name longer than 64 characters", async () => {
    const owner = await insertUser("owner-1");
    await expectPgError(
      db.insert(guilds).values({ name: "x".repeat(65), ownerId: owner.id }),
      /guilds_name_length/
    );
  });

  it("rejects an empty name", async () => {
    const owner = await insertUser("owner-2");
    await expectPgError(db.insert(guilds).values({ name: "", ownerId: owner.id }), /guilds_name_length/);
  });

  it("accepts a valid name and defaults created_at/updated_at", async () => {
    const owner = await insertUser("owner-3");
    const [guild] = await db.insert(guilds).values({ name: "My Guild", ownerId: owner.id }).returning();
    expect(guild.name).toBe("My Guild");
    expect(guild.createdAt).toBeInstanceOf(Date);
  });

  it("restricts deleting a user who still owns a guild", async () => {
    const owner = await insertUser("owner-4");
    await db.insert(guilds).values({ name: "My Guild", ownerId: owner.id });
    await expectPgError(
      db.delete(users).where(sql`${users.id} = ${owner.id}`),
      /violates foreign key constraint/i
    );
  });
});

describe("guild_memberships", () => {
  it("rejects a role outside owner/member", async () => {
    const owner = await insertUser("owner-5");
    const [guild] = await db.insert(guilds).values({ name: "My Guild", ownerId: owner.id }).returning();
    await expectPgError(
      db.insert(guildMemberships).values({ guildId: guild.id, userId: owner.id, role: "admin" }),
      /guild_memberships_role_check/
    );
  });

  it("rejects a duplicate (guild_id, user_id) membership", async () => {
    const owner = await insertUser("owner-6");
    const member = await insertUser("member-6");
    const [guild] = await db.insert(guilds).values({ name: "My Guild", ownerId: owner.id }).returning();
    await db.insert(guildMemberships).values({ guildId: guild.id, userId: member.id });
    await expectPgError(
      db.insert(guildMemberships).values({ guildId: guild.id, userId: member.id }),
      /duplicate key value/i
    );
  });

  it("is removed when the guild is deleted (cascade)", async () => {
    const owner = await insertUser("owner-7");
    const [guild] = await db.insert(guilds).values({ name: "My Guild", ownerId: owner.id }).returning();
    await db.insert(guildMemberships).values({ guildId: guild.id, userId: owner.id });

    await db.delete(channels).where(sql`${channels.guildId} = ${guild.id}`);
    await db.execute(sql`DELETE FROM ${guilds} WHERE ${guilds.id} = ${guild.id}`);

    const remaining = await db
      .select()
      .from(guildMemberships)
      .where(sql`${guildMemberships.guildId} = ${guild.id}`);
    expect(remaining).toHaveLength(0);
  });
});

describe("channels", () => {
  it("only accepts TEXT or VOICE as channel_type", async () => {
    const owner = await insertUser("owner-8");
    const [guild] = await db.insert(guilds).values({ name: "My Guild", ownerId: owner.id }).returning();
    await expectPgError(
      db.insert(channels).values({ guildId: guild.id, name: "general", channelType: "AUDIO" as never }),
      /invalid input value for enum/i
    );
  });

  it("is removed when its guild is deleted (cascade)", async () => {
    const owner = await insertUser("owner-9");
    const [guild] = await db.insert(guilds).values({ name: "My Guild", ownerId: owner.id }).returning();
    await db.insert(channels).values({ guildId: guild.id, name: "general", channelType: "TEXT" });

    await db.execute(sql`DELETE FROM ${guilds} WHERE ${guilds.id} = ${guild.id}`);

    const remaining = await db.select().from(channels).where(sql`${channels.guildId} = ${guild.id}`);
    expect(remaining).toHaveLength(0);
  });
});

describe("sessions", () => {
  it("is removed when its user is deleted (cascade)", async () => {
    const user = await insertUser("user-10");
    await db.insert(sessions).values({
      userId: user.id,
      expiresAt: new Date(Date.now() + 60_000),
      refreshTokenHash: "hash"
    });

    await db.delete(users).where(sql`${users.id} = ${user.id}`);

    const remaining = await db.select().from(sessions).where(sql`${sessions.userId} = ${user.id}`);
    expect(remaining).toHaveLength(0);
  });
});
