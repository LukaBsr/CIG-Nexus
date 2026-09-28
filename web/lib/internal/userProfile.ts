import { eq } from "drizzle-orm";

import { db } from "@/db/client";
import { users } from "@/db/schema";

import { resolveAvatarUrl, resolveDisplayName } from "../user/profile";
import { fromUserWireId, toUserWireId } from "./wireIds";

export interface WireUserProfileSummary {
  user_id: string;
  username: string;
  display_name: string;
  avatar_url: string | null;
}

// docs/social/friends-dms-design.md §4.5, revised at implementation: unlike
// LIST_MEMBERS/LIST_FRIENDS/FETCH_HISTORY (all live internal-API reads
// already), CHAT_MESSAGE/CHANNEL_MESSAGE/DM_MESSAGE are built in C++
// straight from Session's IDENTIFY-time-cached identity (session.username,
// from the JWT — see IdentifyHandler.cpp), with no per-message internal
// API call, by design (persistence is fire-and-forget, after the
// broadcast). Getting display_name/avatar_url onto those live sends
// therefore needs the same Session-hydration-at-IDENTIFY treatment already
// used for guild_ids/friend_ids/blocked_user_ids, not just another column
// on an existing live-join query — this is that hydration call's backing
// endpoint. Returns null only for a malformed/nonexistent user_id.
export async function getUserProfileSummary(userWireId: string): Promise<WireUserProfileSummary | null> {
  const userId = fromUserWireId(userWireId);
  if (!userId) {
    return null;
  }
  const [user] = await db.select().from(users).where(eq(users.id, userId));
  if (!user) {
    return null;
  }
  return {
    user_id: toUserWireId(user.id),
    username: user.discordUsername,
    display_name: resolveDisplayName(user),
    avatar_url: resolveAvatarUrl(user)
  };
}
