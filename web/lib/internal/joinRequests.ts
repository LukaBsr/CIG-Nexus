import { and, eq } from "drizzle-orm";

import { db } from "@/db/client";
import { guildJoinRequests, guildMemberships, guilds, users } from "@/db/schema";

import { InvalidReferenceError } from "./catalog";
import { kMemberRank } from "./roleThemes";
import { fromGuildWireId, fromUserWireId, toUserWireId } from "./wireIds";

export interface WireJoinRequest {
  user_id: string;
  username: string;
  requested_at: string;
}

export type CreateJoinRequestResult = "created" | "already_pending";

// docs/guilds/social-presence-design.md §1.9 (REQUEST_JOIN). Visibility/membership
// checks (guild is `application`, caller isn't already a member) happen in
// C++ against GuildManager's cache and Session state before this is ever
// called — this only owns the row itself and its uniqueness.
export async function createJoinRequest(
  guildWireId: string,
  userWireId: string
): Promise<CreateJoinRequestResult> {
  const guildId = fromGuildWireId(guildWireId);
  const userId = fromUserWireId(userWireId);
  if (!guildId || !userId) {
    throw new InvalidReferenceError("invalid guild_id or user_id");
  }

  const inserted = await db
    .insert(guildJoinRequests)
    .values({ guildId, userId })
    .onConflictDoNothing({ target: [guildJoinRequests.guildId, guildJoinRequests.userId] })
    .returning();
  return inserted.length > 0 ? "created" : "already_pending";
}

// docs/guilds/social-presence-design.md §1.9 (LIST_JOIN_REQUESTS). Returns null
// only when the guild itself doesn't exist.
export async function listJoinRequests(guildWireId: string): Promise<WireJoinRequest[] | null> {
  const guildId = fromGuildWireId(guildWireId);
  if (!guildId) {
    return null;
  }

  const [guildRow] = await db.select({ id: guilds.id }).from(guilds).where(eq(guilds.id, guildId));
  if (!guildRow) {
    return null;
  }

  const rows = await db
    .select({
      userId: guildJoinRequests.userId,
      username: users.discordUsername,
      requestedAt: guildJoinRequests.requestedAt
    })
    .from(guildJoinRequests)
    .innerJoin(users, eq(guildJoinRequests.userId, users.id))
    .where(eq(guildJoinRequests.guildId, guildId));

  return rows.map((r) => ({
    user_id: toUserWireId(r.userId),
    username: r.username,
    requested_at: r.requestedAt.toISOString()
  }));
}

// docs/guilds/social-presence-design.md §1.9 (APPROVE_JOIN_REQUEST). Creates the
// membership and deletes the request row atomically — same
// single-internal-call pattern §1.3 established for invite redemption.
// Returns null if no such request exists.
export async function approveJoinRequest(
  guildWireId: string,
  userWireId: string
): Promise<{ role_rank: number } | null> {
  const guildId = fromGuildWireId(guildWireId);
  const userId = fromUserWireId(userWireId);
  if (!guildId || !userId) {
    return null;
  }

  return db.transaction(async (tx) => {
    const deleted = await tx
      .delete(guildJoinRequests)
      .where(and(eq(guildJoinRequests.guildId, guildId), eq(guildJoinRequests.userId, userId)))
      .returning();
    if (deleted.length === 0) {
      return null;
    }

    const [membership] = await tx
      .insert(guildMemberships)
      .values({ guildId, userId, roleRank: kMemberRank })
      .onConflictDoNothing({ target: [guildMemberships.guildId, guildMemberships.userId] })
      .returning({ roleRank: guildMemberships.roleRank });
    return { role_rank: membership?.roleRank ?? kMemberRank };
  });
}

// docs/guilds/social-presence-design.md §1.9 (REJECT_JOIN_REQUEST).
export async function rejectJoinRequest(guildWireId: string, userWireId: string): Promise<boolean> {
  const guildId = fromGuildWireId(guildWireId);
  const userId = fromUserWireId(userWireId);
  if (!guildId || !userId) {
    return false;
  }

  const deleted = await db
    .delete(guildJoinRequests)
    .where(and(eq(guildJoinRequests.guildId, guildId), eq(guildJoinRequests.userId, userId)))
    .returning();
  return deleted.length > 0;
}
