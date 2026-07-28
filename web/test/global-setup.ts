import path from "node:path";
import { fileURLToPath } from "node:url";

import { PostgreSqlContainer, type StartedPostgreSqlContainer } from "@testcontainers/postgresql";
import { drizzle } from "drizzle-orm/node-postgres";
import { migrate } from "drizzle-orm/node-postgres/migrator";
import { Pool } from "pg";

// Real Postgres in an ephemeral container, not a mock — schema constraints
// (CHECK, UNIQUE, ON DELETE CASCADE/RESTRICT) only mean something when
// verified against the database engine that actually enforces them.
let container: StartedPostgreSqlContainer | undefined;

const __dirname = path.dirname(fileURLToPath(import.meta.url));

export async function setup(): Promise<void> {
  container = await new PostgreSqlContainer("postgres:16-alpine").start();
  const connectionString = container.getConnectionUri();
  process.env.DATABASE_URL = connectionString;

  const pool = new Pool({ connectionString });
  try {
    await migrate(drizzle(pool), {
      migrationsFolder: path.join(__dirname, "..", "db", "migrations")
    });
  } finally {
    await pool.end();
  }
}

export async function teardown(): Promise<void> {
  await container?.stop();
}
