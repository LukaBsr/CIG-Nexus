import { sql } from "drizzle-orm";
import { pgTable, uuid, text, integer, timestamp, index, check } from "drizzle-orm/pg-core";

import { guilds } from "./guilds";
import { users } from "./users";

// docs/guilds/social-presence-design.md §1.2. No per-redemption audit table this
// iteration — use_count is an aggregate counter, not a log of who redeemed
// when.
export const guildInvites = pgTable(
  "guild_invites",
  {
    id: uuid("id").primaryKey().defaultRandom(),
    guildId: uuid("guild_id")
      .notNull()
      .references(() => guilds.id, { onDelete: "cascade" }),
    // URL-safe random token (web/lib/internal/inviteCodes.ts), not derived
    // from guildId or createdAt — see §1.2's entropy note.
    code: text("code").notNull().unique(),
    createdBy: uuid("created_by")
      .notNull()
      .references(() => users.id, { onDelete: "cascade" }),
    maxUses: integer("max_uses"), // NULL = unlimited
    useCount: integer("use_count").notNull().default(0),
    expiresAt: timestamp("expires_at", { withTimezone: true }), // NULL = never expires
    revokedAt: timestamp("revoked_at", { withTimezone: true }), // NULL = active
    createdAt: timestamp("created_at", { withTimezone: true }).notNull().defaultNow()
  },
  (table) => [
    index("idx_guild_invites_guild_id").on(table.guildId),
    check("guild_invites_max_uses_positive", sql`${table.maxUses} IS NULL OR ${table.maxUses} > 0`),
    check("guild_invites_use_count_non_negative", sql`${table.useCount} >= 0`)
  ]
);
