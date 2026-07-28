import { db } from "@/db/client";
import { users } from "@/db/schema";

import type { DiscordUser } from "../discord";

export async function upsertDiscordUser(discordUser: DiscordUser): Promise<typeof users.$inferSelect> {
  const [user] = await db
    .insert(users)
    .values({
      discordId: discordUser.id,
      discordUsername: discordUser.username,
      discordGlobalName: discordUser.global_name,
      discordAvatarHash: discordUser.avatar
    })
    .onConflictDoUpdate({
      target: users.discordId,
      set: {
        discordUsername: discordUser.username,
        discordGlobalName: discordUser.global_name,
        discordAvatarHash: discordUser.avatar,
        updatedAt: new Date()
      }
    })
    .returning();
  return user;
}
