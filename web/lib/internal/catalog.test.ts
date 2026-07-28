import { sql } from "drizzle-orm";
import { afterEach, describe, expect, it } from "vitest";

import { db } from "@/db/client";
import { channels, guildMemberships, guilds, sessions, users } from "@/db/schema";

import {
  createChannel,
  createGuild,
  createMembership,
  deleteChannel,
  deleteGuild,
  deleteMembership,
  getCatalog,
  getRevokedSessionIds,
  InvalidReferenceError
} from "./catalog";
import { toUserWireId } from "./wireIds";

afterEach(async () => {
  await db.execute(
    sql`TRUNCATE TABLE ${channels}, ${guildMemberships}, ${guilds}, ${sessions}, ${users} RESTART IDENTITY CASCADE`
  );
});

async function insertUser(discordId: string) {
  const [user] = await db
    .insert(users)
    .values({ discordId, discordUsername: `user_${discordId}` })
    .returning();
  return user;
}

describe("createGuild", () => {
  it("creates the guild and auto-adds the owner as a member with role owner", async () => {
    const owner = await insertUser("1");
    const guild = await createGuild("My Guild", toUserWireId(owner.id));

    expect(guild.name).toBe("My Guild");
    expect(guild.owner_id).toBe(toUserWireId(owner.id));

    const memberships = await db
      .select()
      .from(guildMemberships)
      .where(sql`${guildMemberships.userId} = ${owner.id}`);
    expect(memberships).toHaveLength(1);
    expect(memberships[0].role).toBe("owner");
  });

  it("rejects an owner_id that isn't a valid wire id", async () => {
    await expect(createGuild("My Guild", "not-a-wire-id")).rejects.toThrow(InvalidReferenceError);
  });
});

describe("deleteGuild", () => {
  it("cascades to channels and memberships", async () => {
    const owner = await insertUser("2");
    const guild = await createGuild("My Guild", toUserWireId(owner.id));
    await createChannel(guild.guild_id, "general", "TEXT");

    const deleted = await deleteGuild(guild.guild_id);
    expect(deleted).toBe(true);

    const remainingChannels = await db.select().from(channels);
    const remainingMemberships = await db.select().from(guildMemberships);
    expect(remainingChannels).toHaveLength(0);
    expect(remainingMemberships).toHaveLength(0);
  });

  it("returns false for an unknown guild", async () => {
    await expect(deleteGuild("g_00000000-0000-0000-0000-000000000000")).resolves.toBe(false);
  });
});

describe("createMembership / deleteMembership", () => {
  it("adds and removes a member", async () => {
    const owner = await insertUser("3");
    const member = await insertUser("4");
    const guild = await createGuild("My Guild", toUserWireId(owner.id));

    await createMembership(guild.guild_id, toUserWireId(member.id));
    let catalog = await getCatalog();
    expect(catalog.memberships).toContainEqual({
      guild_id: guild.guild_id,
      user_id: toUserWireId(member.id)
    });

    const removed = await deleteMembership(guild.guild_id, toUserWireId(member.id));
    expect(removed).toBe(true);

    catalog = await getCatalog();
    expect(catalog.memberships).not.toContainEqual({
      guild_id: guild.guild_id,
      user_id: toUserWireId(member.id)
    });
  });
});

describe("createChannel / deleteChannel", () => {
  it("creates and deletes a channel", async () => {
    const owner = await insertUser("5");
    const guild = await createGuild("My Guild", toUserWireId(owner.id));

    const channel = await createChannel(guild.guild_id, "general", "TEXT");
    expect(channel.guild_id).toBe(guild.guild_id);
    expect(channel.channel_type).toBe("TEXT");

    const deleted = await deleteChannel(channel.channel_id);
    expect(deleted).toBe(true);
    expect(await deleteChannel(channel.channel_id)).toBe(false);
  });
});

describe("getCatalog", () => {
  it("reflects the full current state", async () => {
    const owner = await insertUser("6");
    const guild = await createGuild("My Guild", toUserWireId(owner.id));
    await createChannel(guild.guild_id, "general", "TEXT");

    const catalog = await getCatalog();
    expect(catalog.guilds).toHaveLength(1);
    expect(catalog.channels).toHaveLength(1);
    expect(catalog.memberships).toHaveLength(1);
  });
});

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
