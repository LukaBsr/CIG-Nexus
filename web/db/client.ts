import { Pool } from "pg";
import { drizzle } from "drizzle-orm/node-postgres";

import * as schema from "./schema";

// Next.js dev-mode module reloading needs the pool attached to `globalThis`,
// otherwise every hot reload opens a fresh pool and eventually exhausts
// Postgres's connection limit.
declare global {
  var __cigNexusPgPool: Pool | undefined;
}

const pool =
  global.__cigNexusPgPool ??
  new Pool({
    connectionString: process.env.DATABASE_URL
  });

if (process.env.NODE_ENV !== "production") {
  global.__cigNexusPgPool = pool;
}

export const db = drizzle(pool, { schema });
