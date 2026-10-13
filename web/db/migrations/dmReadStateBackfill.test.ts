import { readFileSync, readdirSync } from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

import { PostgreSqlContainer, type StartedPostgreSqlContainer } from "@testcontainers/postgresql";
import { Pool } from "pg";
import { afterAll, beforeAll, describe, expect, it } from "vitest";

// Tests migration 0009's backfill directly against the pre-migration state
// it's meant to fix — not reachable through the shared testcontainer in
// test/global-setup.ts, which applies every migration (including 0009) to
// a database that never had any pre-existing rows to backfill in the
// first place. This spins up its own short-lived Postgres, replays every
// migration file's raw SQL up to and including 0008 (the schema as it was
// the moment before this PR), inserts fixture rows exactly as an
// already-running production database would have them, then applies 0009
// and asserts what it backfilled.

const __dirname = path.dirname(fileURLToPath(import.meta.url));

function statementsOf(sqlFilePath: string): string[] {
  return readFileSync(sqlFilePath, "utf8")
    .split("--> statement-breakpoint")
    .map((s) => s.trim())
    .filter((s) => s.length > 0);
}

async function applyMigrationFile(pool: Pool, sqlFilePath: string): Promise<void> {
  for (const statement of statementsOf(sqlFilePath)) {
    await pool.query(statement);
  }
}

let container: StartedPostgreSqlContainer;
let pool: Pool;

beforeAll(async () => {
  container = await new PostgreSqlContainer("postgres:16-alpine").start();
  pool = new Pool({ connectionString: container.getConnectionUri() });

  const migrationsDir = path.join(__dirname);
  const preExistingMigrations = readdirSync(migrationsDir)
    .filter((f) => f.endsWith(".sql") && f !== "0009_powerful_chimera.sql")
    .sort();
  for (const file of preExistingMigrations) {
    await applyMigrationFile(pool, path.join(migrationsDir, file));
  }
}, 120_000);

afterAll(async () => {
  await pool.end();
  await container.stop();
});

async function insertUser(discordId: string): Promise<string> {
  const result = await pool.query(
    `INSERT INTO users (discord_id, discord_username) VALUES ($1, $2) RETURNING id`,
    [discordId, `user_${discordId}`]
  );
  return result.rows[0].id as string;
}

async function insertConversation(userIdA: string, userIdB: string): Promise<string> {
  const [a, b] = userIdA < userIdB ? [userIdA, userIdB] : [userIdB, userIdA];
  const result = await pool.query(
    `INSERT INTO dm_conversations (user_id_a, user_id_b) VALUES ($1, $2) RETURNING id`,
    [a, b]
  );
  return result.rows[0].id as string;
}

async function insertMessage(dmConversationId: string, userId: string, seq: number): Promise<void> {
  await pool.query(
    `INSERT INTO messages (dm_conversation_id, user_id, content, seq) VALUES ($1, $2, $3, $4)`,
    [dmConversationId, userId, "hello", seq]
  );
}

describe("migration 0009's dm_read_state backfill", () => {
  it("gives both participants a row at the conversation's max seq, and 0 for an empty conversation", async () => {
    const alice = await insertUser("alice");
    const bob = await insertUser("bob");
    const carol = await insertUser("carol");

    const withMessages = await insertConversation(alice, bob);
    await insertMessage(withMessages, alice, 1);
    await insertMessage(withMessages, bob, 2);
    await insertMessage(withMessages, alice, 3);

    const empty = await insertConversation(alice, carol);

    await applyMigrationFile(pool, path.join(__dirname, "0009_powerful_chimera.sql"));

    const rows = await pool.query<{ user_id: string; dm_conversation_id: string; last_read_seq: string }>(
      `SELECT user_id, dm_conversation_id, last_read_seq FROM dm_read_state ORDER BY dm_conversation_id, user_id`
    );

    const byConversation = (id: string) =>
      rows.rows
        .filter((r) => r.dm_conversation_id === id)
        .map((r) => ({ userId: r.user_id, lastReadSeq: Number(r.last_read_seq) }))
        .sort((a, b) => a.userId.localeCompare(b.userId));

    expect(byConversation(withMessages)).toEqual(
      [
        { userId: alice, lastReadSeq: 3 },
        { userId: bob, lastReadSeq: 3 }
      ].sort((a, b) => a.userId.localeCompare(b.userId))
    );

    expect(byConversation(empty)).toEqual(
      [
        { userId: alice, lastReadSeq: 0 },
        { userId: carol, lastReadSeq: 0 }
      ].sort((a, b) => a.userId.localeCompare(b.userId))
    );

    expect(rows.rows).toHaveLength(4);
  });
});
