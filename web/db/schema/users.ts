import { pgTable, uuid, text, timestamp } from "drizzle-orm/pg-core";

// One row per Discord-authenticated person. Holds display identity only —
// no Discord access/refresh token is stored; this integration never calls
// Discord's API again after the initial login (design doc §1).
export const users = pgTable("users", {
  id: uuid("id").primaryKey().defaultRandom(),
  discordId: text("discord_id").notNull().unique(),
  discordUsername: text("discord_username").notNull(),
  discordGlobalName: text("discord_global_name"),
  discordAvatarHash: text("discord_avatar_hash"),
  createdAt: timestamp("created_at", { withTimezone: true }).notNull().defaultNow(),
  updatedAt: timestamp("updated_at", { withTimezone: true }).notNull().defaultNow()
});
