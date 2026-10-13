import { sql } from "drizzle-orm";
import { pgTable, uuid, bigint, timestamp, unique, check } from "drizzle-orm/pg-core";

import { dmConversations } from "./dmConversations";
import { users } from "./users";

// docs/social/friends-dms-design.md §3.7. One row per (user, conversation)
// they have ever read into — absent means "nothing read yet", not 0: a row
// is created on the first MARK_DM_READ for that pair, same "missing is a
// real state, not a zero" shape as everything else unread-count-adjacent
// here. lastReadSeq is a plain integer high-water mark into messages.seq's
// shared DM id-space (§3.4) — never a foreign key to a specific message
// row, so there is nothing to dangle if that row were ever deleted (no
// message-deletion feature exists today, but the marker doesn't depend on
// one not existing). Monotonic: a handler-level GREATEST(...), never
// enforced here as a CHECK, since Postgres has no way to compare a new
// value against the row's own previous value in a column constraint.
export const dmReadState = pgTable(
  "dm_read_state",
  {
    id: uuid("id").primaryKey().defaultRandom(),
    userId: uuid("user_id")
      .notNull()
      .references(() => users.id, { onDelete: "cascade" }),
    dmConversationId: uuid("dm_conversation_id")
      .notNull()
      .references(() => dmConversations.id, { onDelete: "cascade" }),
    // bigint/"number" mode, matching messages.seq exactly (§4.2) — same
    // id-space, same safe-up-to-2^53 headroom reasoning.
    lastReadSeq: bigint("last_read_seq", { mode: "number" }).notNull().default(0),
    updatedAt: timestamp("updated_at", { withTimezone: true }).notNull().defaultNow()
  },
  (table) => [
    // The natural key, and the ON CONFLICT target markDmRead's upsert uses.
    unique("dm_read_state_user_id_dm_conversation_id_key").on(table.userId, table.dmConversationId),
    check("dm_read_state_last_read_seq_check", sql`${table.lastReadSeq} >= 0`)
  ]
);
