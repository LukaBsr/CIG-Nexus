import { sql } from "drizzle-orm";
import { pgTable, pgEnum, uuid, text, timestamp, index, check } from "drizzle-orm/pg-core";

import { guilds } from "./guilds";

export const channelTypeEnum = pgEnum("channel_type", ["TEXT", "VOICE"]);

export const channels = pgTable(
  "channels",
  {
    id: uuid("id").primaryKey().defaultRandom(),
    guildId: uuid("guild_id")
      .notNull()
      .references(() => guilds.id, { onDelete: "cascade" }),
    name: text("name").notNull(),
    channelType: channelTypeEnum("channel_type").notNull(),
    createdAt: timestamp("created_at", { withTimezone: true }).notNull().defaultNow(),
    updatedAt: timestamp("updated_at", { withTimezone: true }).notNull().defaultNow()
  },
  (table) => [
    index("idx_channels_guild_id").on(table.guildId),
    check("channels_name_length", sql`char_length(${table.name}) BETWEEN 1 AND 64`)
  ]
);
