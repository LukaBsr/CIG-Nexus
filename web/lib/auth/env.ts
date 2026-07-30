import { readFileSync, statSync } from "node:fs";

function requireEnv(name: string): string {
  const value = process.env[name];
  if (!value) {
    throw new Error(`Missing required environment variable: ${name}`);
  }
  return value;
}

// Matches env var names by convention (SESSION_JWT_PRIVATE_KEY_PATH, not
// AUTH_JWT_PUBLIC_KEY_PATH) rather than hardcoding one name, so any future
// *_PRIVATE_KEY_PATH var read through readRequiredFile() gets the same
// permission check automatically.
function isPrivateKeyPathVar(pathEnvVarName: string): boolean {
  return pathEnvVarName.includes("PRIVATE_KEY");
}

// Group or world read bits (0o040 | 0o004) — docs/security-audit.md §1.3 /
// action item 3: a private key on disk that isn't restricted to its owner
// defeats the point of keeping key material out of process env /
// `docker inspect` output in the first place.
const GROUP_OR_WORLD_READABLE = 0o044;

function requireOwnerOnlyPermissions(path: string): void {
  const mode = statSync(path).mode;
  if ((mode & GROUP_OR_WORLD_READABLE) !== 0) {
    throw new Error(
      `${path} is group- or world-readable; refusing to use it as a private key. Run: chmod 600 ${path}`
    );
  }
}

// SESSION_JWT_PRIVATE_KEY_PATH points at a real .pem file on disk (bind-
// mounted from secrets/, see docker-compose.yml) rather than holding key
// content directly — no more escaped-newline normalization needed, since a
// real file already has real newlines.
function readRequiredFile(pathEnvVarName: string): string {
  const path = requireEnv(pathEnvVarName);
  try {
    if (isPrivateKeyPathVar(pathEnvVarName)) {
      requireOwnerOnlyPermissions(path);
    }
    return readFileSync(path, "utf8");
  } catch (err) {
    throw new Error(
      `Failed to read file at ${pathEnvVarName}=${path}: ${(err as Error).message}`
    );
  }
}

// Centralizes required-env lookups so failures happen at the point of use
// (useful in tests, where env vars are set per-test) with a clear message,
// instead of scattered non-null assertions across route handlers.
export const authEnv = {
  get discordClientId(): string {
    return requireEnv("DISCORD_CLIENT_ID");
  },
  get discordClientSecret(): string {
    return requireEnv("DISCORD_CLIENT_SECRET");
  },
  get discordRedirectUri(): string {
    return requireEnv("DISCORD_REDIRECT_URI");
  },
  get sessionJwtPrivateKey(): string {
    return readRequiredFile("SESSION_JWT_PRIVATE_KEY_PATH");
  },
  get oauthTxnSecret(): string {
    return requireEnv("OAUTH_TXN_SECRET");
  },
  get internalApiSharedSecret(): string {
    return requireEnv("INTERNAL_API_SHARED_SECRET");
  },
  get databaseUrl(): string {
    return requireEnv("DATABASE_URL");
  },
  get redisUrl(): string {
    return requireEnv("REDIS_URL");
  }
};

// The full list instrumentation.ts checks at process startup — kept next to
// authEnv so the two can't drift apart (a var added to one without the
// other silently reintroduces either a lazy-only check or an unchecked one).
export const REQUIRED_ENV_VARS = [
  "DISCORD_CLIENT_ID",
  "DISCORD_CLIENT_SECRET",
  "DISCORD_REDIRECT_URI",
  "DATABASE_URL",
  "REDIS_URL",
  "SESSION_JWT_PRIVATE_KEY_PATH",
  "OAUTH_TXN_SECRET",
  "INTERNAL_API_SHARED_SECRET"
] as const;
