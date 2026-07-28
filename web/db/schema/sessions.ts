import { pgTable, uuid, text, timestamp, inet, index } from "drizzle-orm/pg-core";

import { users } from "./users";

// Application sessions — decoupled from the Discord token (design doc §6).
// session_token JWTs reference a row here via their `sid` claim; this is
// what makes revocation possible.
export const sessions = pgTable(
  "sessions",
  {
    id: uuid("id").primaryKey().defaultRandom(),
    userId: uuid("user_id")
      .notNull()
      .references(() => users.id, { onDelete: "cascade" }),
    issuedAt: timestamp("issued_at", { withTimezone: true }).notNull().defaultNow(),
    expiresAt: timestamp("expires_at", { withTimezone: true }).notNull(),
    // SHA-256 hex digest of the refresh token; the raw value is never stored.
    refreshTokenHash: text("refresh_token_hash").notNull(),
    revokedAt: timestamp("revoked_at", { withTimezone: true }),
    userAgent: text("user_agent"),
    ipAddress: inet("ip_address"),
    createdAt: timestamp("created_at", { withTimezone: true }).notNull().defaultNow()
  },
  (table) => [
    index("idx_sessions_user_id").on(table.userId),
    index("idx_sessions_expires_at").on(table.expiresAt)
  ]
);
