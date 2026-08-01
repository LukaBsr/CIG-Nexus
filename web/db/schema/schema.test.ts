import { sql } from "drizzle-orm";
import { afterEach, describe, expect, it } from "vitest";

import { db } from "../client";
import { channels } from "./channels";
import { guildInvites } from "./guildInvites";
import { guildJoinRequests } from "./guildJoinRequests";
import { guildMemberships } from "./guildMemberships";
import { guilds } from "./guilds";
import { messages } from "./messages";
import { sessions } from "./sessions";
import { users } from "./users";

afterEach(async () => {
  await db.execute(
    sql`TRUNCATE TABLE ${messages}, ${channels}, ${guildInvites}, ${guildJoinRequests}, ${guildMemberships}, ${guilds}, ${sessions}, ${users} RESTART IDENTITY CASCADE`
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

  it("accepts a valid name and defaults created_at/updated_at/role_theme/visibility", async () => {
    const owner = await insertUser("owner-3");
    const [guild] = await db.insert(guilds).values({ name: "My Guild", ownerId: owner.id }).returning();
    expect(guild.name).toBe("My Guild");
    expect(guild.createdAt).toBeInstanceOf(Date);
    expect(guild.roleTheme).toBe("pirate");
    expect(guild.visibility).toBe("open");
  });

  it("accepts application and private visibility", async () => {
    const owner = await insertUser("owner-3b");
    const [applicationGuild] = await db
      .insert(guilds)
      .values({ name: "App Guild", ownerId: owner.id, visibility: "application" })
      .returning();
    expect(applicationGuild.visibility).toBe("application");

    const [privateGuild] = await db
      .insert(guilds)
      .values({ name: "Private Guild", ownerId: owner.id, visibility: "private" })
      .returning();
    expect(privateGuild.visibility).toBe("private");
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
  it("rejects a negative role_rank", async () => {
    const owner = await insertUser("owner-5");
    const [guild] = await db.insert(guilds).values({ name: "My Guild", ownerId: owner.id }).returning();
    await expectPgError(
      db.insert(guildMemberships).values({ guildId: guild.id, userId: owner.id, roleRank: -1 }),
      /guild_memberships_role_rank_check/
    );
  });

  it("defaults role_rank to 0", async () => {
    const owner = await insertUser("owner-5b");
    const [guild] = await db.insert(guilds).values({ name: "My Guild", ownerId: owner.id }).returning();
    const [membership] = await db
      .insert(guildMemberships)
      .values({ guildId: guild.id, userId: owner.id })
      .returning();
    expect(membership.roleRank).toBe(0);
  });

  it("accepts a role_rank above the current highest named tier (no upper bound)", async () => {
    const owner = await insertUser("owner-5c");
    const [guild] = await db.insert(guilds).values({ name: "My Guild", ownerId: owner.id }).returning();
    const [membership] = await db
      .insert(guildMemberships)
      .values({ guildId: guild.id, userId: owner.id, roleRank: 5 })
      .returning();
    expect(membership.roleRank).toBe(5);
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

describe("guild_invites", () => {
  it("defaults use_count to 0 and max_uses/expires_at/revoked_at to null", async () => {
    const owner = await insertUser("owner-invites-1");
    const [guild] = await db.insert(guilds).values({ name: "My Guild", ownerId: owner.id }).returning();
    const [invite] = await db
      .insert(guildInvites)
      .values({ guildId: guild.id, code: "abc123", createdBy: owner.id })
      .returning();
    expect(invite.useCount).toBe(0);
    expect(invite.maxUses).toBeNull();
    expect(invite.expiresAt).toBeNull();
    expect(invite.revokedAt).toBeNull();
  });

  it("rejects a non-positive max_uses", async () => {
    const owner = await insertUser("owner-invites-2");
    const [guild] = await db.insert(guilds).values({ name: "My Guild", ownerId: owner.id }).returning();
    await expectPgError(
      db.insert(guildInvites).values({ guildId: guild.id, code: "abc124", createdBy: owner.id, maxUses: 0 }),
      /guild_invites_max_uses_positive/
    );
  });

  it("rejects a duplicate code", async () => {
    const owner = await insertUser("owner-invites-3");
    const [guild] = await db.insert(guilds).values({ name: "My Guild", ownerId: owner.id }).returning();
    await db.insert(guildInvites).values({ guildId: guild.id, code: "dup-code", createdBy: owner.id });
    await expectPgError(
      db.insert(guildInvites).values({ guildId: guild.id, code: "dup-code", createdBy: owner.id }),
      /duplicate key value/i
    );
  });

  it("is removed when the guild is deleted (cascade)", async () => {
    const owner = await insertUser("owner-invites-4");
    const [guild] = await db.insert(guilds).values({ name: "My Guild", ownerId: owner.id }).returning();
    await db.insert(guildInvites).values({ guildId: guild.id, code: "cascade-code", createdBy: owner.id });

    await db.delete(guildMemberships).where(sql`${guildMemberships.guildId} = ${guild.id}`);
    await db.execute(sql`DELETE FROM ${guilds} WHERE ${guilds.id} = ${guild.id}`);

    const remaining = await db.select().from(guildInvites).where(sql`${guildInvites.guildId} = ${guild.id}`);
    expect(remaining).toHaveLength(0);
  });
});

describe("guild_join_requests", () => {
  it("defaults requested_at", async () => {
    const owner = await insertUser("owner-jr-1");
    const requester = await insertUser("requester-jr-1");
    const [guild] = await db.insert(guilds).values({ name: "My Guild", ownerId: owner.id }).returning();
    const [request] = await db
      .insert(guildJoinRequests)
      .values({ guildId: guild.id, userId: requester.id })
      .returning();
    expect(request.requestedAt).toBeInstanceOf(Date);
  });

  it("rejects a duplicate (guild_id, user_id) request", async () => {
    const owner = await insertUser("owner-jr-2");
    const requester = await insertUser("requester-jr-2");
    const [guild] = await db.insert(guilds).values({ name: "My Guild", ownerId: owner.id }).returning();
    await db.insert(guildJoinRequests).values({ guildId: guild.id, userId: requester.id });
    await expectPgError(
      db.insert(guildJoinRequests).values({ guildId: guild.id, userId: requester.id }),
      /duplicate key value/i
    );
  });

  it("is removed when the guild is deleted (cascade)", async () => {
    const owner = await insertUser("owner-jr-3");
    const requester = await insertUser("requester-jr-3");
    const [guild] = await db.insert(guilds).values({ name: "My Guild", ownerId: owner.id }).returning();
    await db.insert(guildJoinRequests).values({ guildId: guild.id, userId: requester.id });

    await db.delete(guildMemberships).where(sql`${guildMemberships.guildId} = ${guild.id}`);
    await db.execute(sql`DELETE FROM ${guilds} WHERE ${guilds.id} = ${guild.id}`);

    const remaining = await db
      .select()
      .from(guildJoinRequests)
      .where(sql`${guildJoinRequests.guildId} = ${guild.id}`);
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

describe("messages", () => {
  it("rejects content longer than 500 characters", async () => {
    const user = await insertUser("msg-owner-1");
    await expectPgError(
      db.insert(messages).values({ userId: user.id, content: "x".repeat(501), seq: 1 }),
      /messages_content_length/
    );
  });

  it("rejects empty content", async () => {
    const user = await insertUser("msg-owner-2");
    await expectPgError(
      db.insert(messages).values({ userId: user.id, content: "", seq: 1 }),
      /messages_content_length/
    );
  });

  it("accepts a lobby message with a NULL channel_id", async () => {
    const user = await insertUser("msg-owner-3");
    const [message] = await db
      .insert(messages)
      .values({ userId: user.id, content: "hello lobby", seq: 1 })
      .returning();
    expect(message.channelId).toBeNull();
    expect(message.content).toBe("hello lobby");
  });

  it("rejects a duplicate seq within the same channel", async () => {
    const owner = await insertUser("msg-owner-4");
    const [guild] = await db.insert(guilds).values({ name: "My Guild", ownerId: owner.id }).returning();
    const [channel] = await db
      .insert(channels)
      .values({ guildId: guild.id, name: "general", channelType: "TEXT" })
      .returning();
    await db.insert(messages).values({ channelId: channel.id, userId: owner.id, content: "one", seq: 1 });
    await expectPgError(
      db.insert(messages).values({ channelId: channel.id, userId: owner.id, content: "two", seq: 1 }),
      /duplicate key value/i
    );
  });

  it("allows the same seq to appear once in the lobby and once in a channel", async () => {
    const owner = await insertUser("msg-owner-5");
    const [guild] = await db.insert(guilds).values({ name: "My Guild", ownerId: owner.id }).returning();
    const [channel] = await db
      .insert(channels)
      .values({ guildId: guild.id, name: "general", channelType: "TEXT" })
      .returning();
    await db.insert(messages).values({ userId: owner.id, content: "lobby one", seq: 1 });
    // Same seq value (1), but one lobby row (channel_id NULL) and one
    // channel row — separate id-spaces, per §4.2, so this must succeed.
    await db.insert(messages).values({ channelId: channel.id, userId: owner.id, content: "channel one", seq: 1 });
    const rows = await db.select().from(messages);
    expect(rows).toHaveLength(2);
  });

  it("rejects a duplicate seq within the lobby itself", async () => {
    const user = await insertUser("msg-owner-6");
    await db.insert(messages).values({ userId: user.id, content: "one", seq: 1 });
    await expectPgError(
      db.insert(messages).values({ userId: user.id, content: "two", seq: 1 }),
      /duplicate key value/i
    );
  });

  it("is removed when its channel is deleted (cascade)", async () => {
    const owner = await insertUser("msg-owner-7");
    const [guild] = await db.insert(guilds).values({ name: "My Guild", ownerId: owner.id }).returning();
    const [channel] = await db
      .insert(channels)
      .values({ guildId: guild.id, name: "general", channelType: "TEXT" })
      .returning();
    await db.insert(messages).values({ channelId: channel.id, userId: owner.id, content: "hi", seq: 1 });

    await db.execute(sql`DELETE FROM ${channels} WHERE ${channels.id} = ${channel.id}`);

    const remaining = await db.select().from(messages).where(sql`${messages.channelId} = ${channel.id}`);
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
