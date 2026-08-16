import { and, desc, eq } from "drizzle-orm";

import { db } from "@/db/client";
import { dmConversations, messages, users } from "@/db/schema";

import { InvalidReferenceError } from "./catalog";
import { resolveAvatarUrl, resolveDisplayName } from "../user/profile";
import { fromUserWireId, toUserWireId } from "./wireIds";

function orderedPair(a: string, b: string): [string, string] {
  return a < b ? [a, b] : [b, a];
}

// docs/social/friends-dms-design.md §3.4: get-or-create, canonically
// ordered — a DM conversation between two users is exactly one row
// regardless of who sent the first message. Called from the async
// persistence path (messages.ts's createMessage below), not synchronously
// from DM_SEND's hot path (§3.5's "conversations are never addressed by
// id on the wire" — the client only ever supplies the peer's user_id;
// resolving/creating the row is an implementation detail of persisting
// the message, not a prerequisite for broadcasting it).
export async function resolveOrCreateDmConversationId(rawUserIdA: string, rawUserIdB: string): Promise<string> {
  const [a, b] = orderedPair(rawUserIdA, rawUserIdB);

  const [existing] = await db
    .select({ id: dmConversations.id })
    .from(dmConversations)
    .where(and(eq(dmConversations.userIdA, a), eq(dmConversations.userIdB, b)));
  if (existing) {
    return existing.id;
  }

  const [created] = await db
    .insert(dmConversations)
    .values({ userIdA: a, userIdB: b })
    .onConflictDoNothing({ target: [dmConversations.userIdA, dmConversations.userIdB] })
    .returning({ id: dmConversations.id });
  if (created) {
    return created.id;
  }

  // Lost a concurrent-insert race — the conflicting row now exists.
  const [row] = await db
    .select({ id: dmConversations.id })
    .from(dmConversations)
    .where(and(eq(dmConversations.userIdA, a), eq(dmConversations.userIdB, b)));
  return row.id;
}

// docs/social/friends-dms-design.md §3.6: resolves a peer's wire id to the
// canonical dm_conversation_id for (callerWireId, peerWireId), creating
// the row if it doesn't exist yet. Used by both createMessage and
// getMessages (messages.ts) for the DM scope.
export async function resolveDmConversationId(callerWireId: string, peerWireId: string): Promise<string> {
  const callerId = fromUserWireId(callerWireId);
  const peerId = fromUserWireId(peerWireId);
  if (!callerId || !peerId) {
    throw new InvalidReferenceError("invalid caller or peer user_id");
  }
  return resolveOrCreateDmConversationId(callerId, peerId);
}

export interface WireDmConversation {
  peer_id: string;
  // Revised at implementation (docs/social/friends-dms-design.md §4.5): the
  // original design shipped this entry with only peer_id/last_message_at —
  // unrenderable as a conversation list without a name. username is now
  // mandatory here (not optional like display_name/avatar_url) for the
  // same reason it's mandatory on every other roster/list entry.
  username: string;
  display_name: string;
  avatar_url: string | null;
  last_message_at: string | null;
}

// docs/social/friends-dms-design.md §3.5 (LIST_DM_CONVERSATIONS). Returns
// null only for a malformed caller id; an empty array is "no DM
// conversations yet," a distinct, valid state (same reasoning as
// listFriends/listInvites). Deliberately minimal — no unread counts or
// message previews, neither of which was asked for (§3.5).
export async function listDmConversations(userWireId: string): Promise<WireDmConversation[] | null> {
  const userId = fromUserWireId(userWireId);
  if (!userId) {
    return null;
  }

  const asUserA = await db
    .select({ id: dmConversations.id, peerId: dmConversations.userIdB })
    .from(dmConversations)
    .where(eq(dmConversations.userIdA, userId));
  const asUserB = await db
    .select({ id: dmConversations.id, peerId: dmConversations.userIdA })
    .from(dmConversations)
    .where(eq(dmConversations.userIdB, userId));
  const conversations = [...asUserA, ...asUserB];

  const result: WireDmConversation[] = [];
  for (const conversation of conversations) {
    const [lastMessage] = await db
      .select({ createdAt: messages.createdAt })
      .from(messages)
      .where(eq(messages.dmConversationId, conversation.id))
      .orderBy(desc(messages.seq))
      .limit(1);
    const [peer] = await db.select().from(users).where(eq(users.id, conversation.peerId));
    result.push({
      peer_id: toUserWireId(conversation.peerId),
      username: peer.discordUsername,
      display_name: resolveDisplayName(peer),
      avatar_url: resolveAvatarUrl(peer),
      last_message_at: lastMessage?.createdAt.toISOString() ?? null
    });
  }
  return result;
}
