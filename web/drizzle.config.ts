import { defineConfig } from "drizzle-kit";

// Only ever run standalone via the drizzle-kit CLI (`npm run db:generate`/
// `db:migrate`, or the Dockerfile's CMD) — never imported by the Next.js
// app itself, so throwing here at module load is safe: it can't break
// `next build`. A missing DATABASE_URL previously fell back to a
// placeholder connection string, which made `drizzle-kit migrate` hang
// trying to reach a Postgres that was never going to exist, instead of
// failing immediately with a clear cause.
if (!process.env.DATABASE_URL) {
  throw new Error("Missing required environment variable: DATABASE_URL");
}

export default defineConfig({
  dialect: "postgresql",
  schema: "./db/schema/index.ts",
  out: "./db/migrations",
  dbCredentials: {
    url: process.env.DATABASE_URL
  }
});
