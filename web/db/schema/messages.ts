import { sql } from "drizzle-orm";
import { pgTable, uuid, text, timestamp, bigint, check, uniqueIndex, index } from "drizzle-orm/pg-core";

import { channels } from "./channels";
import { dmConversations } from "./dmConversations";
import { users } from "./users";

// docs/guilds/social-presence-design.md §4.2: one table for both CHAT_MESSAGE
// (channel_id NULL, the global lobby) and CHANNEL_MESSAGE (channel_id set)
// — both share an identical shape and differ only in scope.
//
// docs/social/friends-dms-design.md §3.4 adds a third, mutually exclusive
// scope: dm_conversation_id set = a DM. Three scopes total: both NULL =
// lobby, channel_id set = channel, dm_conversation_id set = DM — never
// both channel_id and dm_conversation_id set (messages_at_most_one_scope
// below).
//
// `seq` is server-assigned by the C++ server (§4.3), NOT a Postgres
// bigserial — the C++ server stays authoritative for ordering/broadcast,
// Postgres is purely the durable store. Three separate id-spaces, matching
// ChatHandler/ChannelHandler/DMHandler's existing separate
// `message_counter_` members: one shared across the whole lobby, one
// shared across every channel (not per-channel), one shared across every
// DM conversation (not per-conversation) — same "shared, not per-entity"
// shape as the channel counter (§3.4).
export const messages = pgTable(
  "messages",
  {
    id: uuid("id").primaryKey().defaultRandom(),
    // NULL = global lobby (CHAT_MESSAGE); set = a specific channel (CHANNEL_MESSAGE).
    channelId: uuid("channel_id").references(() => channels.id, { onDelete: "cascade" }),
    // NULL = not a DM; set = a specific DM conversation (DM_SEND).
    dmConversationId: uuid("dm_conversation_id").references(() => dmConversations.id, { onDelete: "cascade" }),
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
    // Scoped to also exclude DM rows (both channel_id and
    // dm_conversation_id NULL means lobby, not just channel_id NULL).
    uniqueIndex("idx_messages_lobby_seq")
      .on(table.seq)
      .where(sql`${table.channelId} IS NULL AND ${table.dmConversationId} IS NULL`),
    // DM id-space uniqueness — a single shared space across every DM
    // conversation, mirroring the channel id-space's shape (§3.4), not a
    // per-conversation one.
    uniqueIndex("idx_messages_dm_seq").on(table.seq).where(sql`${table.dmConversationId} IS NOT NULL`),
    // Pagination access pattern (§4.4/§3.4): "most recent N before some
    // point, per channel/lobby/DM conversation".
    index("idx_messages_channel_created").on(table.channelId, table.seq),
    index("idx_messages_dm_created").on(table.dmConversationId, table.seq),
    check("messages_content_length", sql`char_length(${table.content}) BETWEEN 1 AND 500`),
    check(
      "messages_at_most_one_scope",
      sql`NOT (${table.channelId} IS NOT NULL AND ${table.dmConversationId} IS NOT NULL)`
    )
  ]
);
