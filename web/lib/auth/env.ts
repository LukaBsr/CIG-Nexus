function requireEnv(name: string): string {
  const value = process.env[name];
  if (!value) {
    throw new Error(`Missing required environment variable: ${name}`);
  }
  return value;
}

// PEM values passed through docker-compose/.env files commonly arrive with
// literal "\n" escape sequences instead of real newlines; normalize either
// form to what node:crypto/jose expect.
function normalizePem(value: string): string {
  return value.includes("\\n") ? value.replace(/\\n/g, "\n") : value;
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
    return normalizePem(requireEnv("SESSION_JWT_PRIVATE_KEY"));
  },
  get oauthTxnSecret(): string {
    return requireEnv("OAUTH_TXN_SECRET");
  },
  get internalApiSharedSecret(): string {
    return requireEnv("INTERNAL_API_SHARED_SECRET");
  }
};
