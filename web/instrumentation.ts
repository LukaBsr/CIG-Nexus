// Next.js calls register() once when a server instance starts — in both
// `next dev` and `next start`/server.mjs (no config needed, stable since
// Next 15). This is the fail-fast check: if a required var is missing or
// empty, the process must refuse to start rather than run with a fallback
// and fail confusingly later, mid-request, deep in a route handler.
import { authEnv, REQUIRED_ENV_VARS } from "./lib/auth/env";

function exitWithError(message: string): never {
  // Verified (not assumed): a *thrown* error from register() gets logged by
  // Next.js but does not reliably stop the server from going on to bind its
  // ports and serve requests anyway — observed directly by testing this
  // with required vars unset. process.exit() is what actually guarantees
  // "the process refuses to start."
  console.error(message);
  process.exit(1);
}

export async function register(): Promise<void> {
  // Skip in the edge runtime — this app has none, but the guard is the
  // documented-correct way to make sure this Node-only check (and the
  // authEnv module it imports) never runs somewhere without process.env.
  if (process.env.NEXT_RUNTIME !== "nodejs") {
    return;
  }

  const missing = REQUIRED_ENV_VARS.filter((name) => !process.env[name]);
  if (missing.length > 0) {
    exitWithError(
      `Refusing to start: missing required environment variable(s): ${missing.join(", ")}. See .env.example.`
    );
  }

  // SESSION_JWT_PRIVATE_KEY_PATH being set (checked above) only proves the
  // *path string* is non-empty — it says nothing about whether the file it
  // points at (bind-mounted from secrets/, see docker-compose.yml) actually
  // exists and is readable. Read it now, at startup, rather than only
  // discovering a bad mount the first time a route needs to sign a token.
  try {
    void authEnv.sessionJwtPrivateKey;
  } catch (error) {
    exitWithError(`Refusing to start: ${(error as Error).message}`);
  }
}
