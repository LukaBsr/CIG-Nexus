import { sql } from "drizzle-orm";
import { pgTable, uuid, timestamp, index, unique, check } from "drizzle-orm/pg-core";

import { users } from "./users";

// docs/social/friends-dms-design.md §1.2. Directed, pending only — a row
// here always means "requester has asked recipient to be friends and is
// waiting." Deleted (not archived) on accept/reject/cancel; no audit
// table, same reasoning as guild_invites' "no per-redemption audit table."
export const friendRequests = pgTable(
  "friend_requests",
  {
    id: uuid("id").primaryKey().defaultRandom(),
    requesterId: uuid("requester_id")
      .notNull()
      .references(() => users.id, { onDelete: "cascade" }),
    recipientId: uuid("recipient_id")
      .notNull()
      .references(() => users.id, { onDelete: "cascade" }),
    createdAt: timestamp("created_at", { withTimezone: true }).notNull().defaultNow()
  },
  (table) => [
    index("idx_friend_requests_recipient").on(table.recipientId),
    unique("friend_requests_requester_id_recipient_id_key").on(table.requesterId, table.recipientId),
    check("friend_requests_not_self", sql`${table.requesterId} <> ${table.recipientId}`)
  ]
);
