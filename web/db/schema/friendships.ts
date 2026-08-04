import { sql } from "drizzle-orm";
import { pgTable, uuid, timestamp, index, unique, check } from "drizzle-orm/pg-core";

import { users } from "./users";

// docs/social/friends-dms-design.md §1.2. Symmetric, canonically ordered
// so a friendship is exactly one row regardless of who sent the original
// request — userIdA is always the lexicographically smaller UUID,
// enforced by the CHECK, not left to application discipline alone. The
// same ordering convention dm_conversations (§3.4) will reuse.
export const friendships = pgTable(
  "friendships",
  {
    id: uuid("id").primaryKey().defaultRandom(),
    userIdA: uuid("user_id_a")
      .notNull()
      .references(() => users.id, { onDelete: "cascade" }),
    userIdB: uuid("user_id_b")
      .notNull()
      .references(() => users.id, { onDelete: "cascade" }),
    createdAt: timestamp("created_at", { withTimezone: true }).notNull().defaultNow()
  },
  (table) => [
    index("idx_friendships_b").on(table.userIdB),
    unique("friendships_user_id_a_user_id_b_key").on(table.userIdA, table.userIdB),
    check("friendships_ordered_pair", sql`${table.userIdA} < ${table.userIdB}`)
  ]
);
