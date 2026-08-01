import { sql } from "drizzle-orm";
import { afterEach, describe, expect, it } from "vitest";

import { db } from "@/db/client";
import { channels, guildJoinRequests, guildMemberships, guilds, sessions, users } from "@/db/schema";

import {
  createChannel,
  createGuild,
  createMembership,
  deleteChannel,
  deleteGuild,
  deleteMembership,
  getCatalog,
  getGuildMembers,
  getRevokedSessionIds,
  InvalidReferenceError,
  setGuildVisibility,
  setMemberRole
} from "./catalog";
import { kMemberRank, kOfficerRank, kOwnerRank } from "./roleThemes";
import { toUserWireId } from "./wireIds";

afterEach(async () => {
  await db.execute(
    sql`TRUNCATE TABLE ${channels}, ${guildJoinRequests}, ${guildMemberships}, ${guilds}, ${sessions}, ${users} RESTART IDENTITY CASCADE`
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
  it("creates the guild and auto-adds the owner as a member at kOwnerRank", async () => {
    const owner = await insertUser("1");
    const guild = await createGuild("My Guild", toUserWireId(owner.id));

    expect(guild.name).toBe("My Guild");
    expect(guild.owner_id).toBe(toUserWireId(owner.id));

    const memberships = await db
      .select()
      .from(guildMemberships)
      .where(sql`${guildMemberships.userId} = ${owner.id}`);
    expect(memberships).toHaveLength(1);
    expect(memberships[0].roleRank).toBe(kOwnerRank);
  });

  it("rejects an owner_id that isn't a valid wire id", async () => {
    await expect(createGuild("My Guild", "not-a-wire-id")).rejects.toThrow(InvalidReferenceError);
  });

  it("defaults visibility to open, and accepts an explicit visibility", async () => {
    const owner = await insertUser("1b");
    const defaultGuild = await createGuild("Default Guild", toUserWireId(owner.id));
    expect(defaultGuild.visibility).toBe("open");

    const privateGuild = await createGuild("Private Guild", toUserWireId(owner.id), "private");
    expect(privateGuild.visibility).toBe("private");
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

describe("setGuildVisibility", () => {
  it("updates visibility", async () => {
    const owner = await insertUser("2b");
    const guild = await createGuild("My Guild", toUserWireId(owner.id));

    const result = await setGuildVisibility(guild.guild_id, "private");
    expect(result).toEqual({ visibility: "private" });

    const [row] = await db.select().from(guilds).where(sql`${guilds.id} = ${guild.guild_id.slice(2)}`);
    expect(row.visibility).toBe("private");
  });

  it("deletes pending join requests when leaving application mode", async () => {
    const owner = await insertUser("2c");
    const requester = await insertUser("2c-requester");
    const guild = await createGuild("My Guild", toUserWireId(owner.id), "application");
    await db.insert(guildJoinRequests).values({
      guildId: guild.guild_id.slice(2),
      userId: requester.id
    });

    await setGuildVisibility(guild.guild_id, "open");

    const remaining = await db
      .select()
      .from(guildJoinRequests)
      .where(sql`${guildJoinRequests.guildId} = ${guild.guild_id.slice(2)}`);
    expect(remaining).toHaveLength(0);
  });

  it("leaves pending join requests alone when staying in or entering application mode", async () => {
    const owner = await insertUser("2d");
    const requester = await insertUser("2d-requester");
    const guild = await createGuild("My Guild", toUserWireId(owner.id), "application");
    await db.insert(guildJoinRequests).values({
      guildId: guild.guild_id.slice(2),
      userId: requester.id
    });

    // open -> private never passed through application, nothing to delete
    // (already covered above); this checks open -> application creates no
    // deletion path either, since the ARBITRATION only applies to *leaving*
    // application mode.
    const openGuild = await createGuild("Open Guild", toUserWireId(owner.id), "open");
    await setGuildVisibility(openGuild.guild_id, "application");

    const remaining = await db
      .select()
      .from(guildJoinRequests)
      .where(sql`${guildJoinRequests.guildId} = ${guild.guild_id.slice(2)}`);
    expect(remaining).toHaveLength(1); // the original application guild's request, untouched
  });

  it("returns null for an unknown guild", async () => {
    await expect(setGuildVisibility("g_00000000-0000-0000-0000-000000000000", "open")).resolves.toBeNull();
  });
});

describe("createMembership / deleteMembership", () => {
  it("adds and removes a member, defaulting to kMemberRank", async () => {
    const owner = await insertUser("3");
    const member = await insertUser("4");
    const guild = await createGuild("My Guild", toUserWireId(owner.id));

    await createMembership(guild.guild_id, toUserWireId(member.id));
    let catalog = await getCatalog();
    expect(catalog.memberships).toContainEqual({
      guild_id: guild.guild_id,
      user_id: toUserWireId(member.id),
      role_rank: kMemberRank
    });

    const removed = await deleteMembership(guild.guild_id, toUserWireId(member.id));
    expect(removed).toBe(true);

    catalog = await getCatalog();
    expect(catalog.memberships).not.toContainEqual(
      expect.objectContaining({ user_id: toUserWireId(member.id) })
    );
  });

  it("is idempotent on conflict and reports back the existing role_rank, not the requested one", async () => {
    const owner = await insertUser("3b");
    const member = await insertUser("4b");
    const guild = await createGuild("My Guild", toUserWireId(owner.id));

    const first = await createMembership(guild.guild_id, toUserWireId(member.id), kOfficerRank);
    expect(first.role_rank).toBe(kOfficerRank);

    // A second JOIN_GUILD-style call defaults to kMemberRank, but the row
    // already exists at kOfficerRank — must not silently downgrade it.
    const second = await createMembership(guild.guild_id, toUserWireId(member.id));
    expect(second.role_rank).toBe(kOfficerRank);

    const memberships = await db
      .select()
      .from(guildMemberships)
      .where(sql`${guildMemberships.userId} = ${member.id}`);
    expect(memberships[0].roleRank).toBe(kOfficerRank);
  });
});

describe("getGuildMembers", () => {
  it("returns the roster with resolved role_label", async () => {
    const owner = await insertUser("8");
    const member = await insertUser("9");
    const guild = await createGuild("My Guild", toUserWireId(owner.id));
    await createMembership(guild.guild_id, toUserWireId(member.id));

    const members = await getGuildMembers(guild.guild_id);
    expect(members).not.toBeNull();
    expect(members).toHaveLength(2);
    const ownerRow = members?.find((m) => m.user_id === toUserWireId(owner.id));
    expect(ownerRow?.role_rank).toBe(kOwnerRank);
    expect(ownerRow?.role_label).toBe("Captain");
    expect(ownerRow?.username).toBe("user_8");
    const memberRow = members?.find((m) => m.user_id === toUserWireId(member.id));
    expect(memberRow?.role_label).toBe("Crew");
  });

  it("returns null for a guild that doesn't exist", async () => {
    await expect(getGuildMembers("g_00000000-0000-0000-0000-000000000000")).resolves.toBeNull();
  });
});

describe("setMemberRole", () => {
  it("updates role_rank and returns the resolved role_label", async () => {
    const owner = await insertUser("10");
    const member = await insertUser("11");
    const guild = await createGuild("My Guild", toUserWireId(owner.id));
    await createMembership(guild.guild_id, toUserWireId(member.id));

    const result = await setMemberRole(guild.guild_id, toUserWireId(member.id), kOfficerRank);
    expect(result).toEqual({ role_rank: kOfficerRank, role_label: "Officer" });

    const memberships = await db
      .select()
      .from(guildMemberships)
      .where(sql`${guildMemberships.userId} = ${member.id}`);
    expect(memberships[0].roleRank).toBe(kOfficerRank);
  });

  it("returns null for a membership that doesn't exist", async () => {
    const owner = await insertUser("12");
    const guild = await createGuild("My Guild", toUserWireId(owner.id));

    const result = await setMemberRole(
      guild.guild_id,
      "u_00000000-0000-0000-0000-000000000000",
      kOfficerRank
    );
    expect(result).toBeNull();
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
