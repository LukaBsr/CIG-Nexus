import { sql } from "drizzle-orm";
import { afterEach, describe, expect, it } from "vitest";

import { db } from "@/db/client";
import { channels, guildInvites, guildJoinRequests, guildMemberships, guilds, users } from "@/db/schema";

import { createGuild } from "./catalog";
import { approveJoinRequest, createJoinRequest, listJoinRequests, rejectJoinRequest } from "./joinRequests";
import { kMemberRank } from "./roleThemes";
import { toUserWireId } from "./wireIds";

afterEach(async () => {
  await db.execute(
    sql`TRUNCATE TABLE ${channels}, ${guildInvites}, ${guildJoinRequests}, ${guildMemberships}, ${guilds}, ${users} RESTART IDENTITY CASCADE`
  );
});

async function insertUser(discordId: string) {
  const [user] = await db
    .insert(users)
    .values({ discordId, discordUsername: `user_${discordId}` })
    .returning();
  return user;
}

describe("createJoinRequest", () => {
  it("creates a request", async () => {
    const owner = await insertUser("1");
    const requester = await insertUser("1-requester");
    const guild = await createGuild("My Guild", toUserWireId(owner.id), "application");

    await expect(createJoinRequest(guild.guild_id, toUserWireId(requester.id))).resolves.toBe("created");
  });

  it("is idempotent for a duplicate request", async () => {
    const owner = await insertUser("2");
    const requester = await insertUser("2-requester");
    const guild = await createGuild("My Guild", toUserWireId(owner.id), "application");

    await createJoinRequest(guild.guild_id, toUserWireId(requester.id));
    await expect(createJoinRequest(guild.guild_id, toUserWireId(requester.id))).resolves.toBe(
      "already_pending"
    );

    const requests = await db
      .select()
      .from(guildJoinRequests)
      .where(sql`${guildJoinRequests.userId} = ${requester.id}`);
    expect(requests).toHaveLength(1);
  });
});

describe("listJoinRequests", () => {
  it("returns the requesters with resolved username", async () => {
    const owner = await insertUser("3");
    const requester = await insertUser("3-requester");
    const guild = await createGuild("My Guild", toUserWireId(owner.id), "application");
    await createJoinRequest(guild.guild_id, toUserWireId(requester.id));

    const requests = await listJoinRequests(guild.guild_id);
    expect(requests).toHaveLength(1);
    expect(requests?.[0].user_id).toBe(toUserWireId(requester.id));
    expect(requests?.[0].username).toBe(`user_${requester.discordId}`);
  });

  it("returns an empty array for a guild with no pending requests", async () => {
    const owner = await insertUser("4");
    const guild = await createGuild("My Guild", toUserWireId(owner.id), "application");
    await expect(listJoinRequests(guild.guild_id)).resolves.toEqual([]);
  });

  it("returns null for a guild that doesn't exist", async () => {
    await expect(listJoinRequests("g_00000000-0000-0000-0000-000000000000")).resolves.toBeNull();
  });
});

describe("approveJoinRequest", () => {
  it("creates the membership and deletes the request atomically", async () => {
    const owner = await insertUser("5");
    const requester = await insertUser("5-requester");
    const guild = await createGuild("My Guild", toUserWireId(owner.id), "application");
    await createJoinRequest(guild.guild_id, toUserWireId(requester.id));

    const result = await approveJoinRequest(guild.guild_id, toUserWireId(requester.id));
    expect(result).toEqual({ role_rank: kMemberRank });

    const memberships = await db
      .select()
      .from(guildMemberships)
      .where(sql`${guildMemberships.userId} = ${requester.id}`);
    expect(memberships).toHaveLength(1);
    const requests = await db
      .select()
      .from(guildJoinRequests)
      .where(sql`${guildJoinRequests.userId} = ${requester.id}`);
    expect(requests).toHaveLength(0);
  });

  it("returns null for a request that doesn't exist", async () => {
    const owner = await insertUser("6");
    const guild = await createGuild("My Guild", toUserWireId(owner.id), "application");
    await expect(approveJoinRequest(guild.guild_id, "u_00000000-0000-0000-0000-000000000000")).resolves.toBeNull();
  });
});

describe("rejectJoinRequest", () => {
  it("deletes the request without creating a membership", async () => {
    const owner = await insertUser("7");
    const requester = await insertUser("7-requester");
    const guild = await createGuild("My Guild", toUserWireId(owner.id), "application");
    await createJoinRequest(guild.guild_id, toUserWireId(requester.id));

    await expect(rejectJoinRequest(guild.guild_id, toUserWireId(requester.id))).resolves.toBe(true);

    const memberships = await db
      .select()
      .from(guildMemberships)
      .where(sql`${guildMemberships.userId} = ${requester.id}`);
    expect(memberships).toHaveLength(0);
  });

  it("returns false for a request that doesn't exist", async () => {
    const owner = await insertUser("8");
    const guild = await createGuild("My Guild", toUserWireId(owner.id), "application");
    await expect(
      rejectJoinRequest(guild.guild_id, "u_00000000-0000-0000-0000-000000000000")
    ).resolves.toBe(false);
  });
});
