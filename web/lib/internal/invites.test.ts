import { sql } from "drizzle-orm";
import { afterEach, describe, expect, it } from "vitest";

import { db } from "@/db/client";
import { channels, guildInvites, guildJoinRequests, guildMemberships, guilds, users } from "@/db/schema";

import { createGuild, InvalidReferenceError } from "./catalog";
import { createInvite, listInvites, redeemInvite, revokeInvite } from "./invites";
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

describe("createInvite", () => {
  it("creates an invite defaulting to unlimited/never-expiring", async () => {
    const owner = await insertUser("1");
    const guild = await createGuild("My Guild", toUserWireId(owner.id));

    const invite = await createInvite(guild.guild_id, toUserWireId(owner.id), null, null);

    expect(invite.code).toHaveLength(14); // 10 random bytes, base64url-encoded
    expect(invite.max_uses).toBeNull();
    expect(invite.use_count).toBe(0);
    expect(invite.expires_at).toBeNull();
    expect(invite.revoked_at).toBeNull();
  });

  it("converts expires_in_seconds to an absolute expires_at", async () => {
    const owner = await insertUser("2");
    const guild = await createGuild("My Guild", toUserWireId(owner.id));

    const before = Date.now();
    const invite = await createInvite(guild.guild_id, toUserWireId(owner.id), null, 3600);
    const after = Date.now();

    expect(invite.expires_at).not.toBeNull();
    const expiresAtMs = new Date(invite.expires_at as string).getTime();
    expect(expiresAtMs).toBeGreaterThanOrEqual(before + 3600 * 1000);
    expect(expiresAtMs).toBeLessThanOrEqual(after + 3600 * 1000);
  });

  it("rejects an invalid guild_id", async () => {
    const owner = await insertUser("3");
    await expect(createInvite("not-a-wire-id", toUserWireId(owner.id), null, null)).rejects.toThrow(
      InvalidReferenceError
    );
  });

  it("generates distinct codes across calls", async () => {
    const owner = await insertUser("4");
    const guild = await createGuild("My Guild", toUserWireId(owner.id));

    const first = await createInvite(guild.guild_id, toUserWireId(owner.id), null, null);
    const second = await createInvite(guild.guild_id, toUserWireId(owner.id), null, null);

    expect(first.code).not.toBe(second.code);
  });
});

describe("listInvites", () => {
  it("returns every invite for the guild", async () => {
    const owner = await insertUser("5");
    const guild = await createGuild("My Guild", toUserWireId(owner.id));
    await createInvite(guild.guild_id, toUserWireId(owner.id), null, null);
    await createInvite(guild.guild_id, toUserWireId(owner.id), 5, null);

    const invites = await listInvites(guild.guild_id);
    expect(invites).toHaveLength(2);
  });

  it("returns an empty array for a guild with no invites", async () => {
    const owner = await insertUser("6");
    const guild = await createGuild("My Guild", toUserWireId(owner.id));

    await expect(listInvites(guild.guild_id)).resolves.toEqual([]);
  });

  it("returns null for a guild that doesn't exist", async () => {
    await expect(listInvites("g_00000000-0000-0000-0000-000000000000")).resolves.toBeNull();
  });
});

describe("revokeInvite", () => {
  it("sets revoked_at", async () => {
    const owner = await insertUser("7");
    const guild = await createGuild("My Guild", toUserWireId(owner.id));
    const invite = await createInvite(guild.guild_id, toUserWireId(owner.id), null, null);

    const revoked = await revokeInvite(guild.guild_id, invite.code);
    expect(revoked).toBe(true);

    const [row] = await db.select().from(guildInvites).where(sql`${guildInvites.code} = ${invite.code}`);
    expect(row.revokedAt).not.toBeNull();
  });

  it("returns false for a code that doesn't belong to the given guild", async () => {
    const owner = await insertUser("8");
    const guildA = await createGuild("Guild A", toUserWireId(owner.id));
    const guildB = await createGuild("Guild B", toUserWireId(owner.id));
    const invite = await createInvite(guildA.guild_id, toUserWireId(owner.id), null, null);

    await expect(revokeInvite(guildB.guild_id, invite.code)).resolves.toBe(false);
  });

  it("returns false for an already-revoked invite", async () => {
    const owner = await insertUser("9");
    const guild = await createGuild("My Guild", toUserWireId(owner.id));
    const invite = await createInvite(guild.guild_id, toUserWireId(owner.id), null, null);

    await revokeInvite(guild.guild_id, invite.code);
    await expect(revokeInvite(guild.guild_id, invite.code)).resolves.toBe(false);
  });
});

describe("redeemInvite", () => {
  it("creates a membership at kMemberRank for an open guild", async () => {
    const owner = await insertUser("10");
    const redeemer = await insertUser("10-redeemer");
    const guild = await createGuild("My Guild", toUserWireId(owner.id), "open");
    const invite = await createInvite(guild.guild_id, toUserWireId(owner.id), null, null);

    const result = await redeemInvite(invite.code, toUserWireId(redeemer.id));

    expect(result).toEqual({ ok: true, kind: "member", guild_id: guild.guild_id, role_rank: kMemberRank });
    const [membership] = await db
      .select()
      .from(guildMemberships)
      .where(sql`${guildMemberships.userId} = ${redeemer.id}`);
    expect(membership.roleRank).toBe(kMemberRank);

    const [row] = await db.select().from(guildInvites).where(sql`${guildInvites.code} = ${invite.code}`);
    expect(row.useCount).toBe(1);
  });

  it("creates a membership directly for a private guild (its only door)", async () => {
    const owner = await insertUser("11");
    const redeemer = await insertUser("11-redeemer");
    const guild = await createGuild("My Guild", toUserWireId(owner.id), "private");
    const invite = await createInvite(guild.guild_id, toUserWireId(owner.id), null, null);

    const result = await redeemInvite(invite.code, toUserWireId(redeemer.id));

    expect(result).toEqual({ ok: true, kind: "member", guild_id: guild.guild_id, role_rank: kMemberRank });
  });

  it("creates a join request, not a membership, for an application guild", async () => {
    const owner = await insertUser("12");
    const redeemer = await insertUser("12-redeemer");
    const guild = await createGuild("My Guild", toUserWireId(owner.id), "application");
    const invite = await createInvite(guild.guild_id, toUserWireId(owner.id), null, null);

    const result = await redeemInvite(invite.code, toUserWireId(redeemer.id));

    expect(result).toEqual({ ok: true, kind: "join_request", guild_id: guild.guild_id });
    const memberships = await db
      .select()
      .from(guildMemberships)
      .where(sql`${guildMemberships.userId} = ${redeemer.id}`);
    expect(memberships).toHaveLength(0);
    const requests = await db
      .select()
      .from(guildJoinRequests)
      .where(sql`${guildJoinRequests.userId} = ${redeemer.id}`);
    expect(requests).toHaveLength(1);

    // The invite is still consumed even though membership wasn't granted
    // directly (§1.8's ARBITRATION: invite still requires approval).
    const [row] = await db.select().from(guildInvites).where(sql`${guildInvites.code} = ${invite.code}`);
    expect(row.useCount).toBe(1);
  });

  it("returns not_found for an unknown code", async () => {
    const redeemer = await insertUser("13");
    await expect(redeemInvite("nonexistent-code", toUserWireId(redeemer.id))).resolves.toEqual({
      ok: false,
      error: "not_found"
    });
  });

  it("returns revoked for a revoked invite", async () => {
    const owner = await insertUser("14");
    const redeemer = await insertUser("14-redeemer");
    const guild = await createGuild("My Guild", toUserWireId(owner.id));
    const invite = await createInvite(guild.guild_id, toUserWireId(owner.id), null, null);
    await revokeInvite(guild.guild_id, invite.code);

    await expect(redeemInvite(invite.code, toUserWireId(redeemer.id))).resolves.toEqual({
      ok: false,
      error: "revoked"
    });
  });

  it("returns expired for an expired invite", async () => {
    const owner = await insertUser("15");
    const redeemer = await insertUser("15-redeemer");
    const guild = await createGuild("My Guild", toUserWireId(owner.id));
    const invite = await createInvite(guild.guild_id, toUserWireId(owner.id), null, -1);

    await expect(redeemInvite(invite.code, toUserWireId(redeemer.id))).resolves.toEqual({
      ok: false,
      error: "expired"
    });
  });

  it("returns max_uses_reached once the limit is hit, race-safe under concurrent redemption", async () => {
    const owner = await insertUser("16");
    const redeemerA = await insertUser("16-a");
    const redeemerB = await insertUser("16-b");
    const guild = await createGuild("My Guild", toUserWireId(owner.id));
    const invite = await createInvite(guild.guild_id, toUserWireId(owner.id), 1, null);

    const [resultA, resultB] = await Promise.all([
      redeemInvite(invite.code, toUserWireId(redeemerA.id)),
      redeemInvite(invite.code, toUserWireId(redeemerB.id))
    ]);

    const results = [resultA, resultB];
    const succeeded = results.filter((r) => r.ok);
    const failed = results.filter((r) => !r.ok);
    expect(succeeded).toHaveLength(1);
    expect(failed).toHaveLength(1);
    expect(failed[0]).toEqual({ ok: false, error: "max_uses_reached" });

    const [row] = await db.select().from(guildInvites).where(sql`${guildInvites.code} = ${invite.code}`);
    expect(row.useCount).toBe(1); // not 2 — the race didn't double-spend the last use
  });

  it("returns already_member when the redeemer is already a member", async () => {
    const owner = await insertUser("17");
    const guild = await createGuild("My Guild", toUserWireId(owner.id));
    const invite = await createInvite(guild.guild_id, toUserWireId(owner.id), null, null);

    // The owner is already a member of their own guild.
    await expect(redeemInvite(invite.code, toUserWireId(owner.id))).resolves.toEqual({
      ok: false,
      error: "already_member"
    });
    const [row] = await db.select().from(guildInvites).where(sql`${guildInvites.code} = ${invite.code}`);
    expect(row.useCount).toBe(0); // rejected before the increment
  });

  it("returns max_uses_reached, not already_member, when both are true (§1.3's stated order)", async () => {
    const owner = await insertUser("17b");
    const guild = await createGuild("My Guild", toUserWireId(owner.id));
    // max_uses: 1, already exhausted by someone else — the owner is also
    // already a member, so both failure reasons apply simultaneously.
    const invite = await createInvite(guild.guild_id, toUserWireId(owner.id), 1, null);
    const otherRedeemer = await insertUser("17b-other");
    await redeemInvite(invite.code, toUserWireId(otherRedeemer.id));

    await expect(redeemInvite(invite.code, toUserWireId(owner.id))).resolves.toEqual({
      ok: false,
      error: "max_uses_reached"
    });
  });

  it("rejects an invalid user_id", async () => {
    await expect(redeemInvite("some-code", "not-a-wire-id")).rejects.toThrow(InvalidReferenceError);
  });
});
