import { sql } from "drizzle-orm";
import { pgTable, uuid, text, timestamp, boolean, check } from "drizzle-orm/pg-core";

// One row per Discord-authenticated person. Holds display identity only —
// no Discord access/refresh token is stored; this integration never calls
// Discord's API again after the initial login (design doc §1).
export const users = pgTable(
  "users",
  {
    id: uuid("id").primaryKey().defaultRandom(),
    discordId: text("discord_id").notNull().unique(),
    discordUsername: text("discord_username").notNull(),
    discordGlobalName: text("discord_global_name"),
    discordAvatarHash: text("discord_avatar_hash"),
    // docs/settings/appearance-design.md §3.4: nullable, no DEFAULT — NULL
    // means "sync has never been turned on," treated identically to the
    // default theme by every reader (§2.3's degrade-not-error). Free text,
    // not an enum, deliberately mirroring role_theme
    // (docs/guilds/social-presence-design.md §2.2) rather than guild_visibility's
    // ENUM — themes are purely cosmetic and expected to grow, so adding one
    // should never require a migration.
    theme: text("theme"),
    themeSyncEnabled: boolean("theme_sync_enabled").notNull().default(false),
    // docs/social/friends-dms-design.md §4.1: all five nullable, no
    // DEFAULT — NULL means "never customized," same reasoning as
    // theme/themeSyncEnabled above. Resolution order (display name, avatar)
    // lives in web/lib/user/profile.ts, not here.
    displayName: text("display_name"),
    bio: text("bio"),
    statusMessage: text("status_message"),
    accentColor: text("accent_color"),
    // URL path served by web/app/uploads/avatars/[filename]/route.ts
    // (e.g. "/uploads/avatars/<random>.webp"), not a filesystem path and
    // not a full URL — §4.2's local-disk recommendation. NULL = no custom
    // avatar, falls back to the Discord avatar.
    customAvatarPath: text("custom_avatar_path"),
    // docs/social/friends-dms-design.md §1.2/§1.3: nullable at the schema
    // level only to allow the add-column -> backfill -> tighten migration
    // sequence §1.2 calls for (see the migration this column's addition
    // ships in); every row is expected to have one in practice —
    // backfilled for pre-existing rows, generated inline for new ones at
    // account creation (web/lib/auth/upsertUser.ts).
    friendCode: text("friend_code").unique(),
    createdAt: timestamp("created_at", { withTimezone: true }).notNull().defaultNow(),
    updatedAt: timestamp("updated_at", { withTimezone: true }).notNull().defaultNow()
  },
  (table) => [
    check(
      "users_display_name_length",
      sql`${table.displayName} IS NULL OR char_length(${table.displayName}) BETWEEN 1 AND 32`
    ),
    check("users_bio_length", sql`${table.bio} IS NULL OR char_length(${table.bio}) <= 300`),
    check(
      "users_status_message_length",
      sql`${table.statusMessage} IS NULL OR char_length(${table.statusMessage}) <= 100`
    ),
    check("users_accent_color_format", sql`${table.accentColor} IS NULL OR ${table.accentColor} ~ '^#[0-9a-fA-F]{6}$'`)
  ]
);
