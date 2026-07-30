import { sql } from "drizzle-orm";
import { pgTable, uuid, text, timestamp, index, unique, check } from "drizzle-orm/pg-core";

import { guilds } from "./guilds";
import { users } from "./users";

// `role` is intentionally just today's owner/member split stored as data
// instead of derived from guilds.ownerId, so it's the seam the Future
// Permission Hook (docs/guilds/design.md) can widen later without a schema change.
// It is NOT read as a real permission system yet — canCreateChannel/
// canDeleteChannel still just check for 'owner', per docs/guilds/design.md.
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
    role: text("role").notNull().default("member"),
    joinedAt: timestamp("joined_at", { withTimezone: true }).notNull().defaultNow()
  },
  (table) => [
    index("idx_guild_memberships_user_id").on(table.userId),
    index("idx_guild_memberships_guild_id").on(table.guildId),
    unique("guild_memberships_guild_id_user_id_key").on(table.guildId, table.userId),
    check("guild_memberships_role_check", sql`${table.role} IN ('owner', 'member')`)
  ]
);
