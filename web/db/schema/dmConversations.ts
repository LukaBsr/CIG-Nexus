import { sql } from "drizzle-orm";
import { pgTable, uuid, timestamp, unique, check } from "drizzle-orm/pg-core";

import { users } from "./users";

// docs/social/friends-dms-design.md §3.4. Canonically ordered, exactly
// like friendships (§1.2) — a DM conversation between two users is one
// row regardless of who sent the first message. Never addressed by id on
// the wire (DM_SEND/FETCH_HISTORY both take the peer's user_id); this
// table's id is a pure internal/Postgres implementation detail used only
// to scope messages.dm_conversation_id.
export const dmConversations = pgTable(
  "dm_conversations",
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
    unique("dm_conversations_user_id_a_user_id_b_key").on(table.userIdA, table.userIdB),
    check("dm_conversations_ordered_pair", sql`${table.userIdA} < ${table.userIdB}`)
  ]
);
