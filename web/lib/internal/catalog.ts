import { and, eq, gt, isNotNull } from "drizzle-orm";

import { db } from "@/db/client";
import { channels, guildMemberships, guilds, sessions } from "@/db/schema";

import {
  fromChannelWireId,
  fromGuildWireId,
  fromUserWireId,
  toChannelWireId,
  toGuildWireId,
  toUserWireId
} from "./wireIds";

export class InvalidReferenceError extends Error {}

export interface WireGuild {
  guild_id: string;
  name: string;
  owner_id: string;
}

export interface WireMembership {
  guild_id: string;
  user_id: string;
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
      owner_id: toUserWireId(g.ownerId)
    })),
    memberships: membershipRows.map((m) => ({
      guild_id: toGuildWireId(m.guildId),
      user_id: toUserWireId(m.userId)
    })),
    channels: channelRows.map((c) => ({
      channel_id: toChannelWireId(c.id),
      guild_id: toGuildWireId(c.guildId),
      name: c.name,
      channel_type: c.channelType
    }))
  };
}

// CREATE_GUILD also auto-adds the creator as a member (rooms-spec.md
// decision #2) — done in one transaction so the catalog never has a guild
// with no owner membership row, even under a crash between the two writes.
export async function createGuild(name: string, ownerWireId: string): Promise<WireGuild> {
  const ownerId = fromUserWireId(ownerWireId);
  if (!ownerId) {
    throw new InvalidReferenceError(`invalid owner_id: ${ownerWireId}`);
  }

  return db.transaction(async (tx) => {
    const [guild] = await tx.insert(guilds).values({ name, ownerId }).returning();
    await tx.insert(guildMemberships).values({ guildId: guild.id, userId: ownerId, role: "owner" });
    return {
      guild_id: toGuildWireId(guild.id),
      name: guild.name,
      owner_id: toUserWireId(guild.ownerId)
    };
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

export async function createMembership(
  guildWireId: string,
  userWireId: string,
  role: "owner" | "member" = "member"
): Promise<WireMembership> {
  const guildId = fromGuildWireId(guildWireId);
  const userId = fromUserWireId(userWireId);
  if (!guildId || !userId) {
    throw new InvalidReferenceError("invalid guild_id or user_id");
  }
  await db.insert(guildMemberships).values({ guildId, userId, role });
  return { guild_id: guildWireId, user_id: userWireId };
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
