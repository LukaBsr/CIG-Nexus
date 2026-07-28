import { createHash, randomBytes } from "node:crypto";

import { and, eq, gt, isNull } from "drizzle-orm";

import { db } from "@/db/client";
import { sessions } from "@/db/schema";

// design doc §6: 30-day sliding refresh cookie backing the short-lived
// access JWT.
export const REFRESH_COOKIE = "__session";
const REFRESH_TOKEN_TTL_SECONDS = 30 * 24 * 60 * 60;

export const REFRESH_COOKIE_OPTIONS = {
  httpOnly: true,
  // See lib/auth/oauthTxnCookie.ts for why this is conditional, not
  // unconditional (§9.1 vs. this stack's current lack of TLS).
  secure: process.env.NODE_ENV === "production",
  sameSite: "lax" as const,
  path: "/",
  maxAge: REFRESH_TOKEN_TTL_SECONDS
};

export function generateRefreshToken(): string {
  return randomBytes(32).toString("base64url");
}

// Only the hash is ever persisted (design doc §5 column comment).
export function hashRefreshToken(token: string): string {
  return createHash("sha256").update(token).digest("hex");
}

export type Session = typeof sessions.$inferSelect;

export async function createSession(
  userId: string,
  meta: { userAgent?: string; ipAddress?: string }
): Promise<{ session: Session; refreshToken: string }> {
  const refreshToken = generateRefreshToken();
  const refreshTokenHash = hashRefreshToken(refreshToken);
  const expiresAt = new Date(Date.now() + REFRESH_TOKEN_TTL_SECONDS * 1000);

  const [session] = await db
    .insert(sessions)
    .values({
      userId,
      expiresAt,
      refreshTokenHash,
      userAgent: meta.userAgent,
      ipAddress: meta.ipAddress
    })
    .returning();

  return { session, refreshToken };
}

export async function findActiveSessionByRefreshToken(refreshToken: string): Promise<Session | null> {
  const refreshTokenHash = hashRefreshToken(refreshToken);
  const [session] = await db
    .select()
    .from(sessions)
    .where(
      and(
        eq(sessions.refreshTokenHash, refreshTokenHash),
        isNull(sessions.revokedAt),
        gt(sessions.expiresAt, new Date())
      )
    );
  return session ?? null;
}

export async function revokeSession(sessionId: string): Promise<void> {
  await db.update(sessions).set({ revokedAt: new Date() }).where(eq(sessions.id, sessionId));
}
