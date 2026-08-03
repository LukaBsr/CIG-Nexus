import { pgTable, uuid, text, timestamp, boolean } from "drizzle-orm/pg-core";

// One row per Discord-authenticated person. Holds display identity only —
// no Discord access/refresh token is stored; this integration never calls
// Discord's API again after the initial login (design doc §1).
export const users = pgTable("users", {
  id: uuid("id").primaryKey().defaultRandom(),
  discordId: text("discord_id").notNull().unique(),
  discordUsername: text("discord_username").notNull(),
  discordGlobalName: text("discord_global_name"),
  discordAvatarHash: text("discord_avatar_hash"),
  // docs/settings-appearance-design.md §3.4: nullable, no DEFAULT — NULL
  // means "sync has never been turned on," treated identically to the
  // default theme by every reader (§2.3's degrade-not-error). Free text,
  // not an enum, deliberately mirroring role_theme
  // (docs/social-presence-design.md §2.2) rather than guild_visibility's
  // ENUM — themes are purely cosmetic and expected to grow, so adding one
  // should never require a migration.
  theme: text("theme"),
  themeSyncEnabled: boolean("theme_sync_enabled").notNull().default(false),
  createdAt: timestamp("created_at", { withTimezone: true }).notNull().defaultNow(),
  updatedAt: timestamp("updated_at", { withTimezone: true }).notNull().defaultNow()
});
