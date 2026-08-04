import { and, eq, or } from "drizzle-orm";
import { alias } from "drizzle-orm/pg-core";

import { db } from "@/db/client";
import { friendRequests, friendships, userBlocks, users } from "@/db/schema";

import { InvalidReferenceError } from "./catalog";
import { generateFriendCode } from "./friendCodes";
import { fromUserWireId, toUserWireId } from "./wireIds";

export interface WireFriend {
  user_id: string;
  username: string;
}

export interface WireFriendRequest {
  user_id: string;
  username: string;
  created_at: string;
}

function orderedPair(a: string, b: string): [string, string] {
  return a < b ? [a, b] : [b, a];
}

// docs/social/friends-dms-design.md §1.4's ordered validation: target
// exists -> not self -> blocked either direction -> already friends ->
// reverse-pending auto-accept. Step 3 (blocked) deliberately reuses the
// same "user_not_found" discriminant rather than a distinct one — §2.6's
// silent-failure recommendation: a blocked caller sees exactly what a
// caller targeting a nonexistent user_id would see, indistinguishable.
export type SendFriendRequestResult =
  | { ok: true; kind: "request"; user_id: string; username: string }
  | { ok: true; kind: "friends"; user_id: string; username: string }
  | { ok: false; error: "user_not_found" | "self" | "already_friends" };

async function sendFriendRequestTo(requesterId: string, recipientId: string): Promise<SendFriendRequestResult> {
  if (requesterId === recipientId) {
    return { ok: false, error: "self" };
  }

  return db.transaction(async (tx) => {
    const [recipient] = await tx.select().from(users).where(eq(users.id, recipientId));
    if (!recipient) {
      return { ok: false, error: "user_not_found" };
    }

    const [blockRow] = await tx
      .select({ id: userBlocks.id })
      .from(userBlocks)
      .where(
        or(
          and(eq(userBlocks.blockerId, requesterId), eq(userBlocks.blockedId, recipientId)),
          and(eq(userBlocks.blockerId, recipientId), eq(userBlocks.blockedId, requesterId))
        )
      );
    if (blockRow) {
      return { ok: false, error: "user_not_found" };
    }

    const [a, b] = orderedPair(requesterId, recipientId);
    const [existingFriendship] = await tx
      .select({ id: friendships.id })
      .from(friendships)
      .where(and(eq(friendships.userIdA, a), eq(friendships.userIdB, b)));
    if (existingFriendship) {
      return { ok: false, error: "already_friends" };
    }

    // §1.4 step 5: a pending request already exists in the *reverse*
    // direction — auto-accept instead of creating a second, crossed
    // pending request.
    const [reversePending] = await tx
      .select({ id: friendRequests.id })
      .from(friendRequests)
      .where(and(eq(friendRequests.requesterId, recipientId), eq(friendRequests.recipientId, requesterId)));

    if (reversePending) {
      await tx.delete(friendRequests).where(eq(friendRequests.id, reversePending.id));
      await tx
        .insert(friendships)
        .values({ userIdA: a, userIdB: b })
        .onConflictDoNothing({ target: [friendships.userIdA, friendships.userIdB] });
      return { ok: true, kind: "friends", user_id: toUserWireId(recipient.id), username: recipient.discordUsername };
    }

    await tx
      .insert(friendRequests)
      .values({ requesterId, recipientId })
      .onConflictDoNothing({ target: [friendRequests.requesterId, friendRequests.recipientId] });
    return { ok: true, kind: "request", user_id: toUserWireId(recipient.id), username: recipient.discordUsername };
  });
}

// docs/social/friends-dms-design.md §1.5 (SEND_FRIEND_REQUEST) — target by
// user_id directly.
export async function sendFriendRequest(
  requesterWireId: string,
  recipientWireId: string
): Promise<SendFriendRequestResult> {
  const requesterId = fromUserWireId(requesterWireId);
  const recipientId = fromUserWireId(recipientWireId);
  if (!requesterId || !recipientId) {
    throw new InvalidReferenceError("invalid requester_id or recipient_id");
  }
  return sendFriendRequestTo(requesterId, recipientId);
}

// docs/social/friends-dms-design.md §1.3 (ADD_FRIEND_BY_CODE). Resolves
// the code to a recipient, then runs the exact same transaction body
// sendFriendRequest does (§1.3: "implemented as one shared internal
// function with two callers, not two parallel implementations that could
// drift").
export type AddFriendByCodeResult = SendFriendRequestResult | { ok: false; error: "code_not_found" };

export async function addFriendByCode(requesterWireId: string, code: string): Promise<AddFriendByCodeResult> {
  const requesterId = fromUserWireId(requesterWireId);
  if (!requesterId) {
    throw new InvalidReferenceError(`invalid requester_id: ${requesterWireId}`);
  }

  const [target] = await db.select({ id: users.id }).from(users).where(eq(users.friendCode, code));
  if (!target) {
    return { ok: false, error: "code_not_found" };
  }

  return sendFriendRequestTo(requesterId, target.id);
}

export type AcceptFriendRequestResult =
  | { ok: true; user_id: string; username: string }
  | { ok: false; error: "not_found" };

// docs/social/friends-dms-design.md §1.4 (ACCEPT_FRIEND_REQUEST).
// requesterWireId is the original sender; recipientWireId (the caller) is
// accepting.
export async function acceptFriendRequest(
  recipientWireId: string,
  requesterWireId: string
): Promise<AcceptFriendRequestResult> {
  const recipientId = fromUserWireId(recipientWireId);
  const requesterId = fromUserWireId(requesterWireId);
  if (!recipientId || !requesterId) {
    throw new InvalidReferenceError("invalid recipient_id or requester_id");
  }

  return db.transaction(async (tx) => {
    const [pending] = await tx
      .select({ id: friendRequests.id })
      .from(friendRequests)
      .where(and(eq(friendRequests.requesterId, requesterId), eq(friendRequests.recipientId, recipientId)));
    if (!pending) {
      return { ok: false, error: "not_found" };
    }

    await tx.delete(friendRequests).where(eq(friendRequests.id, pending.id));
    const [a, b] = orderedPair(requesterId, recipientId);
    await tx
      .insert(friendships)
      .values({ userIdA: a, userIdB: b })
      .onConflictDoNothing({ target: [friendships.userIdA, friendships.userIdB] });

    const [requester] = await tx.select().from(users).where(eq(users.id, requesterId));
    return { ok: true, user_id: toUserWireId(requesterId), username: requester?.discordUsername ?? "" };
  });
}

// docs/social/friends-dms-design.md §1.4 (REJECT_FRIEND_REQUEST /
// CANCEL_FRIEND_REQUEST) — same row deletion either way, scoped by which
// party the caller must be for each action (recipient for reject,
// requester for cancel), so a caller can't cancel a request they merely
// received or reject one they sent.
export async function deleteFriendRequest(
  requesterWireId: string,
  recipientWireId: string
): Promise<boolean> {
  const requesterId = fromUserWireId(requesterWireId);
  const recipientId = fromUserWireId(recipientWireId);
  if (!requesterId || !recipientId) {
    return false;
  }

  const deleted = await db
    .delete(friendRequests)
    .where(and(eq(friendRequests.requesterId, requesterId), eq(friendRequests.recipientId, recipientId)))
    .returning();
  return deleted.length > 0;
}

// docs/social/friends-dms-design.md §1.4 (REMOVE_FRIEND).
export async function removeFriend(userWireIdA: string, userWireIdB: string): Promise<boolean> {
  const userIdA = fromUserWireId(userWireIdA);
  const userIdB = fromUserWireId(userWireIdB);
  if (!userIdA || !userIdB) {
    return false;
  }

  const [a, b] = orderedPair(userIdA, userIdB);
  const deleted = await db
    .delete(friendships)
    .where(and(eq(friendships.userIdA, a), eq(friendships.userIdB, b)))
    .returning();
  return deleted.length > 0;
}

// docs/social/friends-dms-design.md §1.5 (LIST_FRIENDS). Returns null only
// when the caller's own id is malformed — an empty array is "no friends
// yet," a distinct, valid state (same reasoning as listInvites).
export async function listFriends(userWireId: string): Promise<WireFriend[] | null> {
  const userId = fromUserWireId(userWireId);
  if (!userId) {
    return null;
  }

  const userA = alias(users, "user_a");
  const userB = alias(users, "user_b");
  const rows = await db
    .select({
      userIdA: friendships.userIdA,
      userIdB: friendships.userIdB,
      usernameA: userA.discordUsername,
      usernameB: userB.discordUsername
    })
    .from(friendships)
    .innerJoin(userA, eq(userA.id, friendships.userIdA))
    .innerJoin(userB, eq(userB.id, friendships.userIdB))
    .where(or(eq(friendships.userIdA, userId), eq(friendships.userIdB, userId)));

  return rows.map((row) => {
    const isCallerA = row.userIdA === userId;
    return {
      user_id: toUserWireId(isCallerA ? row.userIdB : row.userIdA),
      username: isCallerA ? row.usernameB : row.usernameA
    };
  });
}

// docs/social/friends-dms-design.md §1.5 (LIST_FRIEND_REQUESTS).
export async function listFriendRequests(
  userWireId: string
): Promise<{ incoming: WireFriendRequest[]; outgoing: WireFriendRequest[] } | null> {
  const userId = fromUserWireId(userWireId);
  if (!userId) {
    return null;
  }

  const [incomingRows, outgoingRows] = await Promise.all([
    db
      .select({
        userId: friendRequests.requesterId,
        username: users.discordUsername,
        createdAt: friendRequests.createdAt
      })
      .from(friendRequests)
      .innerJoin(users, eq(friendRequests.requesterId, users.id))
      .where(eq(friendRequests.recipientId, userId)),
    db
      .select({
        userId: friendRequests.recipientId,
        username: users.discordUsername,
        createdAt: friendRequests.createdAt
      })
      .from(friendRequests)
      .innerJoin(users, eq(friendRequests.recipientId, users.id))
      .where(eq(friendRequests.requesterId, userId))
  ]);

  const toWire = (rows: typeof incomingRows): WireFriendRequest[] =>
    rows.map((r) => ({ user_id: toUserWireId(r.userId), username: r.username, created_at: r.createdAt.toISOString() }));

  return { incoming: toWire(incomingRows), outgoing: toWire(outgoingRows) };
}

// docs/social/friends-dms-design.md §1.3. Returns null only for a
// malformed user_id — every real user is expected to have a code
// (generated at account creation, backfilled on next login for legacy
// rows, web/lib/auth/upsertUser.ts) but this defensively generates one
// on the spot if somehow still missing, rather than erroring.
export async function fetchFriendCode(userWireId: string): Promise<string | null> {
  const userId = fromUserWireId(userWireId);
  if (!userId) {
    return null;
  }

  const [row] = await db.select({ friendCode: users.friendCode }).from(users).where(eq(users.id, userId));
  if (!row) {
    return null;
  }
  if (row.friendCode) {
    return row.friendCode;
  }

  return regenerateFriendCode(userWireId);
}

export async function regenerateFriendCode(userWireId: string): Promise<string | null> {
  const userId = fromUserWireId(userWireId);
  if (!userId) {
    return null;
  }

  const code = generateFriendCode();
  const [updated] = await db.update(users).set({ friendCode: code }).where(eq(users.id, userId)).returning();
  return updated ? code : null;
}
