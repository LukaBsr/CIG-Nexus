import { and, eq, isNull, or, sql } from "drizzle-orm";

import { db } from "@/db/client";
import { guildInvites, guildJoinRequests, guildMemberships, guilds } from "@/db/schema";

import { InvalidReferenceError, type GuildVisibility } from "./catalog";
import { generateInviteCode } from "./inviteCodes";
import { kMemberRank } from "./roleThemes";
import { fromGuildWireId, fromUserWireId, toGuildWireId } from "./wireIds";

export interface WireInvite {
  code: string;
  max_uses: number | null;
  use_count: number;
  expires_at: string | null;
  revoked_at: string | null;
  created_at: string;
}

function toWireInvite(row: typeof guildInvites.$inferSelect): WireInvite {
  return {
    code: row.code,
    max_uses: row.maxUses,
    use_count: row.useCount,
    expires_at: row.expiresAt?.toISOString() ?? null,
    revoked_at: row.revokedAt?.toISOString() ?? null,
    created_at: row.createdAt.toISOString()
  };
}

// docs/guilds/social-presence-design.md §1.4/§1.6: max_uses/expires_in_seconds
// both optional, defaulting to unlimited/never-expiring — reusable, no
// expiry, by default. expiresInSeconds is converted to an absolute
// expires_at here (server-computed), the same reasoning JWTs already use
// absolute exp rather than a client-supplied duration.
export async function createInvite(
  guildWireId: string,
  createdByWireId: string,
  maxUses: number | null,
  expiresInSeconds: number | null
): Promise<WireInvite> {
  const guildId = fromGuildWireId(guildWireId);
  const createdBy = fromUserWireId(createdByWireId);
  if (!guildId || !createdBy) {
    throw new InvalidReferenceError("invalid guild_id or created_by");
  }

  const expiresAt = expiresInSeconds != null ? new Date(Date.now() + expiresInSeconds * 1000) : null;

  // A nonexistent guild_id surfaces as a foreign-key constraint violation
  // here, same as createChannel — no separate existence pre-check, matching
  // that existing convention.
  const [invite] = await db
    .insert(guildInvites)
    .values({ guildId, code: generateInviteCode(), createdBy, maxUses, expiresAt })
    .returning();
  return toWireInvite(invite);
}

// docs/guilds/social-presence-design.md §1.4 (LIST_INVITES). Returns null only
// when the guild itself doesn't exist — an empty array is a guild with no
// invites, a distinct, valid state.
export async function listInvites(guildWireId: string): Promise<WireInvite[] | null> {
  const guildId = fromGuildWireId(guildWireId);
  if (!guildId) {
    return null;
  }

  const [guildRow] = await db.select({ id: guilds.id }).from(guilds).where(eq(guilds.id, guildId));
  if (!guildRow) {
    return null;
  }

  const rows = await db.select().from(guildInvites).where(eq(guildInvites.guildId, guildId));
  return rows.map(toWireInvite);
}

// docs/guilds/social-presence-design.md §1.4 (REVOKE_INVITE). Scoped to the given
// guild — a code belonging to a different guild must not be revocable via
// this call, matching "invite exists and belongs to guild_id" (§1.4).
export async function revokeInvite(guildWireId: string, code: string): Promise<boolean> {
  const guildId = fromGuildWireId(guildWireId);
  if (!guildId) {
    return false;
  }

  const [updated] = await db
    .update(guildInvites)
    .set({ revokedAt: new Date() })
    .where(and(eq(guildInvites.guildId, guildId), eq(guildInvites.code, code), isNull(guildInvites.revokedAt)))
    .returning();
  return !!updated;
}

export type RedeemInviteResult =
  | { ok: true; kind: "member"; guild_id: string; role_rank: number }
  // docs/guilds/social-presence-design.md §1.8's ARBITRATION (invite still
  // requires approval): redeeming against an `application`-visibility
  // guild consumes the invite but queues a join request instead of
  // granting membership directly.
  | { ok: true; kind: "join_request"; guild_id: string }
  | { ok: false; error: "not_found" | "revoked" | "expired" | "max_uses_reached" | "already_member" };

// docs/guilds/social-presence-design.md §1.3/§1.5: one atomic call — validate,
// increment use_count, and create the membership (or join request) in the
// same transaction, guarded by use_count < max_uses OR max_uses IS NULL in
// the UPDATE's WHERE clause so two concurrent redemptions of the last
// remaining use can't both succeed.
export async function redeemInvite(code: string, userWireId: string): Promise<RedeemInviteResult> {
  const userId = fromUserWireId(userWireId);
  if (!userId) {
    throw new InvalidReferenceError(`invalid user_id: ${userWireId}`);
  }

  return db.transaction(async (tx) => {
    const [invite] = await tx.select().from(guildInvites).where(eq(guildInvites.code, code));
    if (!invite) {
      return { ok: false, error: "not_found" };
    }
    if (invite.revokedAt) {
      return { ok: false, error: "revoked" };
    }
    if (invite.expiresAt && invite.expiresAt.getTime() < Date.now()) {
      return { ok: false, error: "expired" };
    }

    // §1.3's stated validation order checks max_uses before "already a
    // member" — a read-only pre-check here (not yet the atomic guard) so a
    // maxed-out invite reports MAX_USES_REACHED even when the caller
    // happens to already be a member too, matching that order. The actual
    // race-safe guard against concurrent redemptions is the conditional
    // UPDATE below; this pre-check alone would still let a race through
    // (that's exactly why the UPDATE re-checks the same condition
    // atomically) — but it does need to run before the already_member
    // check without mutating anything yet, since incrementing use_count
    // here and returning already_member afterward would wrongly consume a
    // use for a no-op.
    if (invite.maxUses !== null && invite.useCount >= invite.maxUses) {
      return { ok: false, error: "max_uses_reached" };
    }

    const [existingMembership] = await tx
      .select({ userId: guildMemberships.userId })
      .from(guildMemberships)
      .where(and(eq(guildMemberships.guildId, invite.guildId), eq(guildMemberships.userId, userId)));
    if (existingMembership) {
      return { ok: false, error: "already_member" };
    }

    const incremented = await tx
      .update(guildInvites)
      .set({ useCount: sql`${guildInvites.useCount} + 1` })
      .where(
        and(
          eq(guildInvites.id, invite.id),
          or(isNull(guildInvites.maxUses), sql`${guildInvites.useCount} < ${guildInvites.maxUses}`)
        )
      )
      .returning();
    if (incremented.length === 0) {
      // A concurrent redemption won the race between the pre-check above
      // and this atomic, race-safe guard.
      return { ok: false, error: "max_uses_reached" };
    }

    const [guildRow] = await tx
      .select({ visibility: guilds.visibility })
      .from(guilds)
      .where(eq(guilds.id, invite.guildId));
    const visibility: GuildVisibility = guildRow?.visibility ?? "open";

    if (visibility === "application") {
      await tx
        .insert(guildJoinRequests)
        .values({ guildId: invite.guildId, userId })
        .onConflictDoNothing({ target: [guildJoinRequests.guildId, guildJoinRequests.userId] });
      return { ok: true, kind: "join_request", guild_id: toGuildWireId(invite.guildId) };
    }

    const [membership] = await tx
      .insert(guildMemberships)
      .values({ guildId: invite.guildId, userId, roleRank: kMemberRank })
      .onConflictDoNothing({ target: [guildMemberships.guildId, guildMemberships.userId] })
      .returning({ roleRank: guildMemberships.roleRank });
    return {
      ok: true,
      kind: "member",
      guild_id: toGuildWireId(invite.guildId),
      role_rank: membership?.roleRank ?? kMemberRank
    };
  });
}
