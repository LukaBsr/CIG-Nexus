import { sql } from "drizzle-orm";
import { NextRequest } from "next/server";
import { afterEach, describe, expect, it } from "vitest";

import { db } from "@/db/client";
import { sessions, users } from "@/db/schema";
import { createSession } from "@/lib/auth/session";

import { PATCH } from "./route";

afterEach(async () => {
  await db.execute(sql`TRUNCATE TABLE ${sessions}, ${users} RESTART IDENTITY CASCADE`);
});

async function insertUser(discordId: string) {
  const [user] = await db
    .insert(users)
    .values({ discordId, discordUsername: `user_${discordId}` })
    .returning();
  return user;
}

function patchRequest(body: unknown, cookie?: string): NextRequest {
  return new NextRequest("http://localhost:3000/api/user/appearance", {
    method: "PATCH",
    headers: new Headers({
      "content-type": "application/json",
      ...(cookie ? { cookie } : {})
    }),
    body: JSON.stringify(body)
  });
}

describe("PATCH /api/user/appearance", () => {
  it("returns 401 when there is no refresh cookie", async () => {
    const response = await PATCH(patchRequest({ theme: "ember" }));
    expect(response.status).toBe(401);
  });

  it("returns 401 for an unknown refresh token", async () => {
    const response = await PATCH(patchRequest({ theme: "ember" }, "__session=not-a-real-token"));
    expect(response.status).toBe(401);
  });

  it("rejects an unrecognized theme id", async () => {
    const user = await insertUser("1");
    const { refreshToken } = await createSession(user.id, {});

    const response = await PATCH(patchRequest({ theme: "not-a-real-theme" }, `__session=${refreshToken}`));
    expect(response.status).toBe(400);
  });

  it("rejects a non-boolean sync_enabled", async () => {
    const user = await insertUser("2");
    const { refreshToken } = await createSession(user.id, {});

    const response = await PATCH(
      patchRequest({ sync_enabled: "yes" }, `__session=${refreshToken}`)
    );
    expect(response.status).toBe(400);
  });

  it("rejects an empty body", async () => {
    const user = await insertUser("3");
    const { refreshToken } = await createSession(user.id, {});

    const response = await PATCH(patchRequest({}, `__session=${refreshToken}`));
    expect(response.status).toBe(400);
  });

  it("updates theme and returns the resulting row", async () => {
    const user = await insertUser("4");
    const { refreshToken } = await createSession(user.id, {});

    const response = await PATCH(patchRequest({ theme: "ember" }, `__session=${refreshToken}`));
    expect(response.status).toBe(200);
    const body = (await response.json()) as { theme: string | null; sync_enabled: boolean };
    expect(body).toEqual({ theme: "ember", sync_enabled: false });

    const [row] = await db.select().from(users).where(sql`${users.id} = ${user.id}`);
    expect(row.theme).toBe("ember");
  });

  it("updates sync_enabled independently of theme", async () => {
    const user = await insertUser("5");
    const { refreshToken } = await createSession(user.id, {});

    const response = await PATCH(patchRequest({ sync_enabled: true }, `__session=${refreshToken}`));
    expect(response.status).toBe(200);
    const body = (await response.json()) as { theme: string | null; sync_enabled: boolean };
    expect(body).toEqual({ theme: null, sync_enabled: true });
  });

  it("updates both fields together in one call", async () => {
    const user = await insertUser("6");
    const { refreshToken } = await createSession(user.id, {});

    const response = await PATCH(
      patchRequest({ theme: "abyss", sync_enabled: true }, `__session=${refreshToken}`)
    );
    expect(response.status).toBe(200);
    const body = (await response.json()) as { theme: string | null; sync_enabled: boolean };
    expect(body).toEqual({ theme: "abyss", sync_enabled: true });
  });

  it("only ever updates the caller's own row (no user_id field is accepted)", async () => {
    const victim = await insertUser("7");
    const attacker = await insertUser("8");
    const { refreshToken } = await createSession(attacker.id, {});

    await PATCH(
      patchRequest(
        { theme: "ember", user_id: victim.id },
        `__session=${refreshToken}`
      )
    );

    const [victimRow] = await db.select().from(users).where(sql`${users.id} = ${victim.id}`);
    const [attackerRow] = await db.select().from(users).where(sql`${users.id} = ${attacker.id}`);
    expect(victimRow.theme).toBeNull();
    expect(attackerRow.theme).toBe("ember");
  });
});
