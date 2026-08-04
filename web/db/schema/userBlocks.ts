import { sql } from "drizzle-orm";
import { pgTable, uuid, timestamp, index, unique, check } from "drizzle-orm/pg-core";

import { users } from "./users";

// docs/social/friends-dms-design.md §2.2. Directed, not symmetric — A
// blocking B says nothing about whether B has blocked A.
export const userBlocks = pgTable(
  "user_blocks",
  {
    id: uuid("id").primaryKey().defaultRandom(),
    blockerId: uuid("blocker_id")
      .notNull()
      .references(() => users.id, { onDelete: "cascade" }),
    blockedId: uuid("blocked_id")
      .notNull()
      .references(() => users.id, { onDelete: "cascade" }),
    createdAt: timestamp("created_at", { withTimezone: true }).notNull().defaultNow()
  },
  (table) => [
    index("idx_user_blocks_blocked").on(table.blockedId),
    unique("user_blocks_blocker_id_blocked_id_key").on(table.blockerId, table.blockedId),
    check("user_blocks_not_self", sql`${table.blockerId} <> ${table.blockedId}`)
  ]
);
