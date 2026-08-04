import { sql } from "drizzle-orm";

import { db } from "@/db/client";
import { users } from "@/db/schema";
import { generateFriendCode } from "@/lib/internal/friendCodes";

import type { DiscordUser } from "../discord";

export async function upsertDiscordUser(discordUser: DiscordUser): Promise<typeof users.$inferSelect> {
  // docs/social/friends-dms-design.md §1.2/§1.3: generated once per call,
  // used both as the new-account value and — via the COALESCE below — as
  // the self-healing backfill value for a pre-existing row that predates
  // this column (friend_code still NULL). A returning user with an
  // existing code keeps it; the freshly generated value here is simply
  // discarded in that case, cheaper than a separate one-off backfill
  // migration for what's expected to be a small number of legacy rows.
  const friendCode = generateFriendCode();

  const [user] = await db
    .insert(users)
    .values({
      discordId: discordUser.id,
      discordUsername: discordUser.username,
      discordGlobalName: discordUser.global_name,
      discordAvatarHash: discordUser.avatar,
      friendCode
    })
    .onConflictDoUpdate({
      target: users.discordId,
      set: {
        discordUsername: discordUser.username,
        discordGlobalName: discordUser.global_name,
        discordAvatarHash: discordUser.avatar,
        friendCode: sql`COALESCE(${users.friendCode}, ${friendCode})`,
        updatedAt: new Date()
      }
    })
    .returning();
  return user;
}
