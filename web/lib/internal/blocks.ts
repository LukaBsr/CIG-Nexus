import { and, eq, or } from "drizzle-orm";

import { db } from "@/db/client";
import { friendRequests, friendships, userBlocks, users } from "@/db/schema";

import { resolveAvatarUrl, resolveDisplayName } from "../user/profile";
import { fromUserWireId, toUserWireId } from "./wireIds";

export interface WireBlock {
  user_id: string;
  username: string;
  blocked_at: string;
  // docs/social/friends-dms-design.md §4.5.
  display_name: string;
  avatar_url: string | null;
}

function orderedPair(a: string, b: string): [string, string] {
  return a < b ? [a, b] : [b, a];
}

// docs/social/friends-dms-design.md §2.3's transaction: insert the block,
// then cancel any pending friend_requests row between the two (either
// direction) and remove an existing friendship, all atomically. Blocking
// an already-blocked user is idempotent success (ON CONFLICT DO NOTHING),
// not an error (§2.3).
export async function blockUser(blockerWireId: string, blockedWireId: string): Promise<boolean> {
  const blockerId = fromUserWireId(blockerWireId);
  const blockedId = fromUserWireId(blockedWireId);
  if (!blockerId || !blockedId || blockerId === blockedId) {
    return false;
  }

  await db.transaction(async (tx) => {
    await tx
      .insert(userBlocks)
      .values({ blockerId, blockedId })
      .onConflictDoNothing({ target: [userBlocks.blockerId, userBlocks.blockedId] });

    await tx
      .delete(friendRequests)
      .where(
        or(
          and(eq(friendRequests.requesterId, blockerId), eq(friendRequests.recipientId, blockedId)),
          and(eq(friendRequests.requesterId, blockedId), eq(friendRequests.recipientId, blockerId))
        )
      );

    const [a, b] = orderedPair(blockerId, blockedId);
    await tx.delete(friendships).where(and(eq(friendships.userIdA, a), eq(friendships.userIdB, b)));
  });

  return true;
}

// docs/social/friends-dms-design.md §2.7 (UNBLOCK_USER). Does not restore
// any prior friendship/pending request — both parties start from "no
// relationship" (§2.7).
export async function unblockUser(blockerWireId: string, blockedWireId: string): Promise<boolean> {
  const blockerId = fromUserWireId(blockerWireId);
  const blockedId = fromUserWireId(blockedWireId);
  if (!blockerId || !blockedId) {
    return false;
  }

  const deleted = await db
    .delete(userBlocks)
    .where(and(eq(userBlocks.blockerId, blockerId), eq(userBlocks.blockedId, blockedId)))
    .returning();
  return deleted.length > 0;
}

// docs/social/friends-dms-design.md §2.7 (LIST_BLOCKS). Returns null only
// for a malformed caller id.
export async function listBlocks(userWireId: string): Promise<WireBlock[] | null> {
  const userId = fromUserWireId(userWireId);
  if (!userId) {
    return null;
  }

  const rows = await db
    .select({
      blockedId: userBlocks.blockedId,
      username: users.discordUsername,
      createdAt: userBlocks.createdAt,
      displayName: users.displayName,
      discordGlobalName: users.discordGlobalName,
      customAvatarPath: users.customAvatarPath,
      discordId: users.discordId,
      discordAvatarHash: users.discordAvatarHash
    })
    .from(userBlocks)
    .innerJoin(users, eq(userBlocks.blockedId, users.id))
    .where(eq(userBlocks.blockerId, userId));

  return rows.map((r) => ({
    user_id: toUserWireId(r.blockedId),
    username: r.username,
    blocked_at: r.createdAt.toISOString(),
    display_name: resolveDisplayName({ displayName: r.displayName, discordGlobalName: r.discordGlobalName, discordUsername: r.username }),
    avatar_url: resolveAvatarUrl(r)
  }));
}

// docs/social/friends-dms-design.md §1.4 step 3 / §2.4 — checked from both
// friends.ts (sendFriendRequestTo) and the DM permission model (§3, step
// 4). Raw (non-wire) ids, since every caller here already has them
// resolved. "Either direction" per §1.4: a block from either party blocks
// the interaction, regardless of who's attempting it.
export async function isBlockedEitherDirection(userIdA: string, userIdB: string): Promise<boolean> {
  const [row] = await db
    .select({ id: userBlocks.id })
    .from(userBlocks)
    .where(
      or(
        and(eq(userBlocks.blockerId, userIdA), eq(userBlocks.blockedId, userIdB)),
        and(eq(userBlocks.blockerId, userIdB), eq(userBlocks.blockedId, userIdA))
      )
    );
  return !!row;
}

// docs/social/friends-dms-design.md §2.5/§4.4 — the one-directional check
// GET /api/users/:id/profile uses ("has the target blocked the caller").
// Deliberately one-directional, unlike isBlockedEitherDirection above: the
// Scope section's note is explicit that only this direction is built
// (the blocked user loses visibility into the blocker; the reverse — the
// blocker also losing visibility into someone they've chosen to block —
// isn't part of this design). Raw (non-wire) ids.
export async function isBlockedBy(blockerId: string, blockedId: string): Promise<boolean> {
  const [row] = await db
    .select({ id: userBlocks.id })
    .from(userBlocks)
    .where(and(eq(userBlocks.blockerId, blockerId), eq(userBlocks.blockedId, blockedId)));
  return !!row;
}
