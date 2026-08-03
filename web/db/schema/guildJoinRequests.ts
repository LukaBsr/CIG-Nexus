import { pgTable, uuid, timestamp, index, unique } from "drizzle-orm/pg-core";

import { guilds } from "./guilds";
import { users } from "./users";

// docs/guilds/social-presence-design.md §1.9.
export const guildJoinRequests = pgTable(
  "guild_join_requests",
  {
    id: uuid("id").primaryKey().defaultRandom(),
    guildId: uuid("guild_id")
      .notNull()
      .references(() => guilds.id, { onDelete: "cascade" }),
    userId: uuid("user_id")
      .notNull()
      .references(() => users.id, { onDelete: "cascade" }),
    requestedAt: timestamp("requested_at", { withTimezone: true }).notNull().defaultNow()
  },
  (table) => [
    index("idx_guild_join_requests_guild_id").on(table.guildId),
    unique("guild_join_requests_guild_id_user_id_key").on(table.guildId, table.userId)
  ]
);
