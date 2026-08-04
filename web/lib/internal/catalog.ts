import { and, eq, gt, isNotNull } from "drizzle-orm";

import { db } from "@/db/client";
import { channels, guildJoinRequests, guildMemberships, guilds, sessions, users } from "@/db/schema";

import { kMemberRank, kOwnerRank, resolveRoleLabel } from "./roleThemes";
import {
  fromChannelWireId,
  fromGuildWireId,
  fromUserWireId,
  toChannelWireId,
  toGuildWireId,
  toUserWireId
} from "./wireIds";

export class InvalidReferenceError extends Error {}

// docs/guilds/social-presence-design.md §1.7.
export type GuildVisibility = "open" | "application" | "private";

export interface WireGuild {
  guild_id: string;
  name: string;
  owner_id: string;
  visibility: GuildVisibility;
}

export interface WireMembership {
  guild_id: string;
  user_id: string;
  role_rank: number;
}

export interface WireMember {
  user_id: string;
  username: string;
  role_rank: number;
  role_label: string;
  joined_at: string;
}

export interface WireChannel {
  channel_id: string;
  guild_id: string;
  name: string;
  channel_type: "TEXT" | "VOICE";
}

export interface Catalog {
  guilds: WireGuild[];
  memberships: WireMembership[];
  channels: WireChannel[];
}

// design doc §8.1: fetched once at C++ server startup to populate the
// write-through GuildManager cache.
export async function getCatalog(): Promise<Catalog> {
  const [guildRows, membershipRows, channelRows] = await Promise.all([
    db.select().from(guilds),
    db.select().from(guildMemberships),
    db.select().from(channels)
  ]);

  return {
    guilds: guildRows.map((g) => ({
      guild_id: toGuildWireId(g.id),
      name: g.name,
      owner_id: toUserWireId(g.ownerId),
      visibility: g.visibility
    })),
    memberships: membershipRows.map((m) => ({
      guild_id: toGuildWireId(m.guildId),
      user_id: toUserWireId(m.userId),
      role_rank: m.roleRank
    })),
    channels: channelRows.map((c) => ({
      channel_id: toChannelWireId(c.id),
      guild_id: toGuildWireId(c.guildId),
      name: c.name,
      channel_type: c.channelType
    }))
  };
}

// CREATE_GUILD also auto-adds the creator as a member (docs/guilds/design.md
// decision #2) — done in one transaction so the catalog never has a guild
// with no owner membership row, even under a crash between the two writes.
export async function createGuild(
  name: string,
  ownerWireId: string,
  visibility: GuildVisibility = "open"
): Promise<WireGuild> {
  const ownerId = fromUserWireId(ownerWireId);
  if (!ownerId) {
    throw new InvalidReferenceError(`invalid owner_id: ${ownerWireId}`);
  }

  return db.transaction(async (tx) => {
    const [guild] = await tx.insert(guilds).values({ name, ownerId, visibility }).returning();
    await tx
      .insert(guildMemberships)
      .values({ guildId: guild.id, userId: ownerId, roleRank: kOwnerRank });
    return {
      guild_id: toGuildWireId(guild.id),
      name: guild.name,
      owner_id: toUserWireId(guild.ownerId),
      visibility: guild.visibility
    };
  });
}

// docs/guilds/social-presence-design.md §1.10 (SET_GUILD_VISIBILITY). Deleting
// pending join requests is folded into the same transaction as the
// visibility UPDATE — only when leaving `application` mode, per the
// ARBITRATION there (an officer's silence on a pending request shouldn't
// retroactively become approval just because the mode changed for an
// unrelated reason). Returns null if the guild doesn't exist.
export async function setGuildVisibility(
  guildWireId: string,
  visibility: GuildVisibility
): Promise<{ visibility: GuildVisibility } | null> {
  const guildId = fromGuildWireId(guildWireId);
  if (!guildId) {
    return null;
  }

  return db.transaction(async (tx) => {
    const [before] = await tx
      .select({ visibility: guilds.visibility })
      .from(guilds)
      .where(eq(guilds.id, guildId));
    if (!before) {
      return null;
    }

    const [updated] = await tx
      .update(guilds)
      .set({ visibility })
      .where(eq(guilds.id, guildId))
      .returning({ visibility: guilds.visibility });

    if (before.visibility === "application" && visibility !== "application") {
      await tx.delete(guildJoinRequests).where(eq(guildJoinRequests.guildId, guildId));
    }

    return { visibility: updated.visibility };
  });
}

// DELETE_GUILD: cascades to channels/memberships via the schema's
// ON DELETE CASCADE (§5) — no separate cleanup calls needed.
export async function deleteGuild(guildWireId: string): Promise<boolean> {
  const guildId = fromGuildWireId(guildWireId);
  if (!guildId) {
    return false;
  }
  const deleted = await db.delete(guilds).where(eq(guilds.id, guildId)).returning();
  return deleted.length > 0;
}

// Idempotent on (guild_id, user_id): a returning connection's SessionManager
// state (server/include/session/Session.hpp) starts empty on every reconnect
// even though the underlying membership is durable now, so the C++
// GuildHandler's JOIN_GUILD can legitimately call this again for a
// membership that already exists in Postgres from a prior session. That
// must not surface as an error — the client-facing "already a member" check
// still happens against the per-connection SessionManager state, unchanged.
// Returns the *actual* resulting role_rank, not necessarily roleRank: on
// the idempotent-rejoin path (see the comment above) the insert is a no-op
// against an existing row, which may carry a role_rank the caller doesn't
// know about (e.g. a previously-promoted officer rejoining) — silently
// reporting back the requested roleRank instead would let a fresh
// JOIN_GUILD's default kMemberRank clobber GuildManager's cached rank for
// that user, demoting them client-side even though Postgres never changed.
export async function createMembership(
  guildWireId: string,
  userWireId: string,
  roleRank: number = kMemberRank
): Promise<WireMembership> {
  const guildId = fromGuildWireId(guildWireId);
  const userId = fromUserWireId(userWireId);
  if (!guildId || !userId) {
    throw new InvalidReferenceError("invalid guild_id or user_id");
  }
  const [inserted] = await db
    .insert(guildMemberships)
    .values({ guildId, userId, roleRank })
    .onConflictDoNothing({ target: [guildMemberships.guildId, guildMemberships.userId] })
    .returning({ roleRank: guildMemberships.roleRank });

  if (inserted) {
    return { guild_id: guildWireId, user_id: userWireId, role_rank: inserted.roleRank };
  }

  const [existing] = await db
    .select({ roleRank: guildMemberships.roleRank })
    .from(guildMemberships)
    .where(and(eq(guildMemberships.guildId, guildId), eq(guildMemberships.userId, userId)));
  return { guild_id: guildWireId, user_id: userWireId, role_rank: existing?.roleRank ?? roleRank };
}

export async function deleteMembership(guildWireId: string, userWireId: string): Promise<boolean> {
  const guildId = fromGuildWireId(guildWireId);
  const userId = fromUserWireId(userWireId);
  if (!guildId || !userId) {
    return false;
  }
  const deleted = await db
    .delete(guildMemberships)
    .where(and(eq(guildMemberships.guildId, guildId), eq(guildMemberships.userId, userId)))
    .returning();
  return deleted.length > 0;
}

// docs/social/friends-dms-design.md §3.3: the live-fallback query
// canSendDm() falls back to when the peer has zero active connections (no
// in-memory Session.guild_ids to intersect against) — mirrors
// getGuildIdsForUser's in-memory C++ counterpart
// (GuildManager::getGuildIdsForUser), just Postgres-backed instead of
// cache-backed, since this only runs for the rare disconnected-peer case.
// Returns null only for a malformed user_id, not "user has no guilds"
// (empty array).
export async function getGuildIdsForUser(userWireId: string): Promise<string[] | null> {
  const userId = fromUserWireId(userWireId);
  if (!userId) {
    return null;
  }
  const rows = await db.select({ guildId: guildMemberships.guildId }).from(guildMemberships).where(eq(guildMemberships.userId, userId));
  return rows.map((r) => toGuildWireId(r.guildId));
}

// docs/guilds/social-presence-design.md §2.3: LIST_MEMBERS is a live read, not
// cached anywhere — a cold, UI-driven path, unlike the guild/channel
// catalog. Resolves role_label here (not in C++) so the theme mapping
// stays a single server-side concern. Returns null when the guild itself
// doesn't exist, distinct from a guild that exists but has no members.
export async function getGuildMembers(guildWireId: string): Promise<WireMember[] | null> {
  const guildId = fromGuildWireId(guildWireId);
  if (!guildId) {
    return null;
  }

  const [guildRow] = await db.select({ roleTheme: guilds.roleTheme }).from(guilds).where(eq(guilds.id, guildId));
  if (!guildRow) {
    return null;
  }

  const rows = await db
    .select({
      userId: guildMemberships.userId,
      username: users.discordUsername,
      roleRank: guildMemberships.roleRank,
      joinedAt: guildMemberships.joinedAt
    })
    .from(guildMemberships)
    .innerJoin(users, eq(guildMemberships.userId, users.id))
    .where(eq(guildMemberships.guildId, guildId));

  return rows.map((r) => ({
    user_id: toUserWireId(r.userId),
    username: r.username,
    role_rank: r.roleRank,
    role_label: resolveRoleLabel(guildRow.roleTheme, r.roleRank),
    joined_at: r.joinedAt.toISOString()
  }));
}

// docs/guilds/social-presence-design.md §2.4 (SET_MEMBER_ROLE). Structurally
// cannot target the owner's row: C++ refuses the request before this is
// ever called (GuildManager::isOwner check), so this only ever runs
// against a non-owner membership — not re-enforced here as a second gate,
// consistent with how deleteMembership etc. also trust the caller's
// authorization check rather than re-validating it against Postgres.
export async function setMemberRole(
  guildWireId: string,
  userWireId: string,
  roleRank: number
): Promise<{ role_rank: number; role_label: string } | null> {
  const guildId = fromGuildWireId(guildWireId);
  const userId = fromUserWireId(userWireId);
  if (!guildId || !userId) {
    return null;
  }

  const [guildRow] = await db.select({ roleTheme: guilds.roleTheme }).from(guilds).where(eq(guilds.id, guildId));
  if (!guildRow) {
    return null;
  }

  const [updated] = await db
    .update(guildMemberships)
    .set({ roleRank })
    .where(and(eq(guildMemberships.guildId, guildId), eq(guildMemberships.userId, userId)))
    .returning({ roleRank: guildMemberships.roleRank });
  if (!updated) {
    return null;
  }

  return { role_rank: updated.roleRank, role_label: resolveRoleLabel(guildRow.roleTheme, updated.roleRank) };
}

export async function createChannel(
  guildWireId: string,
  name: string,
  channelType: "TEXT" | "VOICE"
): Promise<WireChannel> {
  const guildId = fromGuildWireId(guildWireId);
  if (!guildId) {
    throw new InvalidReferenceError(`invalid guild_id: ${guildWireId}`);
  }
  const [channel] = await db.insert(channels).values({ guildId, name, channelType }).returning();
  return {
    channel_id: toChannelWireId(channel.id),
    guild_id: toGuildWireId(channel.guildId),
    name: channel.name,
    channel_type: channel.channelType
  };
}

export async function deleteChannel(channelWireId: string): Promise<boolean> {
  const channelId = fromChannelWireId(channelWireId);
  if (!channelId) {
    return false;
  }
  const deleted = await db.delete(channels).where(eq(channels.id, channelId)).returning();
  return deleted.length > 0;
}

// design doc §9: backs the C++ server's poll-based revocation cache. Only
// explicit revocations, not natural expiry — the access JWT's own `exp`
// already handles that (§6).
export async function getRevokedSessionIds(since: Date): Promise<string[]> {
  const rows = await db
    .select({ id: sessions.id })
    .from(sessions)
    .where(and(isNotNull(sessions.revokedAt), gt(sessions.revokedAt, since)));
  return rows.map((r) => r.id);
}
