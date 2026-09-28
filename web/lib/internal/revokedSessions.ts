import { and, gt, isNotNull } from "drizzle-orm";

import { db } from "@/db/client";
import { sessions } from "@/db/schema";

// design doc §9: backs the C++ server's poll-based revocation cache. Only
// explicit revocations, not natural expiry — the access JWT's own `exp`
// already handles that (§6).
export async function getRevokedSessionIds(since: Date): Promise<string[]> {
  const rows = await db
    .select({ id: sessions.id })
    .from(sessions)
    .where(and(isNotNull(sessions.revokedAt), gt(sessions.revokedAt, since)));
  return rows.map((r) => r.id);
}
