import { sql } from "drizzle-orm";
import { pgTable, uuid, text, timestamp, bigint, check, uniqueIndex, index } from "drizzle-orm/pg-core";

import { channels } from "./channels";
import { users } from "./users";

// docs/guilds/social-presence-design.md §4.2: one table for both CHAT_MESSAGE
// (channel_id NULL, the global lobby) and CHANNEL_MESSAGE (channel_id set)
// — both share an identical shape and differ only in scope.
//
// `seq` is server-assigned by the C++ server (§4.3), NOT a Postgres
// bigserial — the C++ server stays authoritative for ordering/broadcast,
// Postgres is purely the durable store. Two separate id-spaces, matching
// ChatHandler/ChannelHandler's existing separate `message_counter_`
// members: one shared across the whole lobby, one shared across every
// channel (not per-channel).
export const messages = pgTable(
  "messages",
  {
    id: uuid("id").primaryKey().defaultRandom(),
    // NULL = global lobby (CHAT_MESSAGE); set = a specific channel (CHANNEL_MESSAGE).
    channelId: uuid("channel_id").references(() => channels.id, { onDelete: "cascade" }),
    userId: uuid("user_id")
      .notNull()
      .references(() => users.id, { onDelete: "cascade" }),
    content: text("content").notNull(),
    // bigint/"number" mode: safe up to 2^53, comfortably enough headroom for
    // a per-process sequence counter; avoids BigInt<->JSON friction a
    // "bigint" mode column would otherwise introduce.
    seq: bigint("seq", { mode: "number" }).notNull(),
    createdAt: timestamp("created_at", { withTimezone: true }).notNull().defaultNow()
  },
  (table) => [
    // Per-channel id-space uniqueness.
    uniqueIndex("idx_messages_channel_seq")
      .on(table.channelId, table.seq)
      .where(sql`${table.channelId} IS NOT NULL`),
    // Lobby id-space uniqueness — a plain UNIQUE(channel_id, seq) doesn't
    // work here since Postgres treats every NULL channel_id as distinct.
    uniqueIndex("idx_messages_lobby_seq").on(table.seq).where(sql`${table.channelId} IS NULL`),
    // Pagination access pattern (§4.4): "most recent N before some point,
    // per channel/lobby".
    index("idx_messages_channel_created").on(table.channelId, table.seq),
    check("messages_content_length", sql`char_length(${table.content}) BETWEEN 1 AND 500`)
  ]
);
