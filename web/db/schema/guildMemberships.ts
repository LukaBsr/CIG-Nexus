import { sql } from "drizzle-orm";
import { pgTable, uuid, integer, timestamp, index, unique, check } from "drizzle-orm/pg-core";

import { guilds } from "./guilds";
import { users } from "./users";

// docs/guilds/social-presence-design.md §2.2: `role` (string, owner/member) is
// replaced by `roleRank` (integer, no upper bound or enumeration —
// deliberately, so adding a tier is an additive constants change, never a
// migration touching this constraint). See server/include/guild/RoleRank.hpp
// for the named rank constants predicates compare against; nothing here or
// in C++ ever compares roleRank against a literal.
export const guildMemberships = pgTable(
  "guild_memberships",
  {
    id: uuid("id").primaryKey().defaultRandom(),
    guildId: uuid("guild_id")
      .notNull()
      .references(() => guilds.id, { onDelete: "cascade" }),
    userId: uuid("user_id")
      .notNull()
      .references(() => users.id, { onDelete: "cascade" }),
    roleRank: integer("role_rank").notNull().default(0),
    joinedAt: timestamp("joined_at", { withTimezone: true }).notNull().defaultNow()
  },
  (table) => [
    index("idx_guild_memberships_user_id").on(table.userId),
    index("idx_guild_memberships_guild_id").on(table.guildId),
    unique("guild_memberships_guild_id_user_id_key").on(table.guildId, table.userId),
    check("guild_memberships_role_rank_check", sql`${table.roleRank} >= 0`)
  ]
);
