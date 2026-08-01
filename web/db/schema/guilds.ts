import { sql } from "drizzle-orm";
import { pgTable, uuid, text, timestamp, index, check } from "drizzle-orm/pg-core";

import { users } from "./users";

// CIG-Nexus's own Guild entity (docs/guilds/design.md), now durable.
export const guilds = pgTable(
  "guilds",
  {
    id: uuid("id").primaryKey().defaultRandom(),
    name: text("name").notNull(),
    // ON DELETE RESTRICT: deleting a user with guilds they own must be an
    // explicit decision (transfer or cascade-delete the guild first), not
    // an accidental side effect of a user-deletion path.
    ownerId: uuid("owner_id")
      .notNull()
      .references(() => users.id, { onDelete: "restrict" }),
    // docs/social-presence-design.md §2.2: purely cosmetic — resolves
    // role_rank -> role_label per guild (web/lib/internal/roleThemes.ts).
    // Never read by any permission predicate.
    roleTheme: text("role_theme").notNull().default("pirate"),
    createdAt: timestamp("created_at", { withTimezone: true }).notNull().defaultNow(),
    updatedAt: timestamp("updated_at", { withTimezone: true }).notNull().defaultNow()
  },
  (table) => [
    index("idx_guilds_owner_id").on(table.ownerId),
    check("guilds_name_length", sql`char_length(${table.name}) BETWEEN 1 AND 64`)
  ]
);
