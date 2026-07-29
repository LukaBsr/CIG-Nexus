import { Pool } from "pg";
import { drizzle } from "drizzle-orm/node-postgres";

import * as schema from "./schema";

// Next.js dev-mode module reloading needs the pool attached to `globalThis`,
// otherwise every hot reload opens a fresh pool and eventually exhausts
// Postgres's connection limit.
declare global {
  var __cigNexusPgPool: Pool | undefined;
}

// Deliberately reads process.env directly here rather than throwing via
// authEnv.databaseUrl: this module is imported at build time too (`next
// build`'s page-data collection actually executes top-level module code,
// not just type-checks it), when no real DATABASE_URL exists yet — an
// import-time throw here would break the Docker build. The actual
// fail-fast guarantee lives in instrumentation.ts's register(), which
// checks DATABASE_URL is set before the server starts serving any request
// that would reach this pool.
const pool =
  global.__cigNexusPgPool ??
  new Pool({
    connectionString: process.env.DATABASE_URL
  });

if (process.env.NODE_ENV !== "production") {
  global.__cigNexusPgPool = pool;
}

export const db = drizzle(pool, { schema });
