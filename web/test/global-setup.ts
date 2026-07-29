import path from "node:path";
import { fileURLToPath } from "node:url";

import { PostgreSqlContainer, type StartedPostgreSqlContainer } from "@testcontainers/postgresql";
import { RedisContainer, type StartedRedisContainer } from "@testcontainers/redis";
import { drizzle } from "drizzle-orm/node-postgres";
import { migrate } from "drizzle-orm/node-postgres/migrator";
import { Pool } from "pg";

// Real Postgres and Redis in ephemeral containers, not mocks — schema
// constraints (CHECK, UNIQUE, ON DELETE CASCADE/RESTRICT) and the rate
// limiter's Lua script only mean something when verified against the
// engines that actually run them.
let pgContainer: StartedPostgreSqlContainer | undefined;
let redisContainer: StartedRedisContainer | undefined;

const __dirname = path.dirname(fileURLToPath(import.meta.url));

export async function setup(): Promise<void> {
  [pgContainer, redisContainer] = await Promise.all([
    new PostgreSqlContainer("postgres:16-alpine").start(),
    new RedisContainer("redis:7-alpine").start()
  ]);

  const connectionString = pgContainer.getConnectionUri();
  process.env.DATABASE_URL = connectionString;
  process.env.REDIS_URL = redisContainer.getConnectionUrl();

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
  await Promise.all([pgContainer?.stop(), redisContainer?.stop()]);
}
