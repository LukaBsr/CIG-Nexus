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

async function insertUser(discordId: string, extra?: Partial<typeof users.$inferInsert>) {
  const [user] = await db
    .insert(users)
    .values({ discordId, discordUsername: `user_${discordId}`, ...extra })
    .returning();
  return user;
}

function patchRequest(body: unknown, cookie?: string): NextRequest {
  return new NextRequest("http://localhost:3000/api/user/profile", {
    method: "PATCH",
    headers: new Headers({
      "content-type": "application/json",
      ...(cookie ? { cookie } : {})
    }),
    body: JSON.stringify(body)
  });
}

describe("PATCH /api/user/profile", () => {
  it("returns 401 when there is no refresh cookie", async () => {
    const response = await PATCH(patchRequest({ display_name: "Nova" }));
    expect(response.status).toBe(401);
  });

  it("returns 401 for an unknown refresh token", async () => {
    const response = await PATCH(patchRequest({ display_name: "Nova" }, "__session=not-a-real-token"));
    expect(response.status).toBe(401);
  });

  it("rejects an empty body", async () => {
    const user = await insertUser("1");
    const { refreshToken } = await createSession(user.id, {});

    const response = await PATCH(patchRequest({}, `__session=${refreshToken}`));
    expect(response.status).toBe(400);
  });

  it("rejects a display_name over 32 characters", async () => {
    const user = await insertUser("2");
    const { refreshToken } = await createSession(user.id, {});

    const response = await PATCH(patchRequest({ display_name: "x".repeat(33) }, `__session=${refreshToken}`));
    expect(response.status).toBe(400);
  });

  it("rejects an empty-string display_name (use null to clear)", async () => {
    const user = await insertUser("3");
    const { refreshToken } = await createSession(user.id, {});

    const response = await PATCH(patchRequest({ display_name: "" }, `__session=${refreshToken}`));
    expect(response.status).toBe(400);
  });

  it("rejects a bio over 300 characters", async () => {
    const user = await insertUser("4");
    const { refreshToken } = await createSession(user.id, {});

    const response = await PATCH(patchRequest({ bio: "x".repeat(301) }, `__session=${refreshToken}`));
    expect(response.status).toBe(400);
  });

  it("rejects a status_message over 100 characters", async () => {
    const user = await insertUser("5");
    const { refreshToken } = await createSession(user.id, {});

    const response = await PATCH(patchRequest({ status_message: "x".repeat(101) }, `__session=${refreshToken}`));
    expect(response.status).toBe(400);
  });

  it("rejects a malformed accent_color", async () => {
    const user = await insertUser("6");
    const { refreshToken } = await createSession(user.id, {});

    const response = await PATCH(patchRequest({ accent_color: "teal" }, `__session=${refreshToken}`));
    expect(response.status).toBe(400);
  });

  it("accepts a well-formed accent_color", async () => {
    const user = await insertUser("7");
    const { refreshToken } = await createSession(user.id, {});

    const response = await PATCH(patchRequest({ accent_color: "#5eead4" }, `__session=${refreshToken}`));
    expect(response.status).toBe(200);
    const body = (await response.json()) as { accent_color: string | null };
    expect(body.accent_color).toBe("#5eead4");
  });

  it("updates all four fields together and returns the resolved display_name", async () => {
    const user = await insertUser("8");
    const { refreshToken } = await createSession(user.id, {});

    const response = await PATCH(
      patchRequest(
        { display_name: "Nova", bio: "hi", status_message: "afk", accent_color: "#8b5cf6" },
        `__session=${refreshToken}`
      )
    );
    expect(response.status).toBe(200);
    const body = (await response.json()) as {
      display_name: string;
      bio: string | null;
      status_message: string | null;
      accent_color: string | null;
      avatar_url: string | null;
    };
    expect(body).toEqual({
      display_name: "Nova",
      bio: "hi",
      status_message: "afk",
      accent_color: "#8b5cf6",
      avatar_url: null
    });
  });

  it("clears a field back to its fallback when set to null", async () => {
    const user = await insertUser("9", { displayName: "Nova" });
    const { refreshToken } = await createSession(user.id, {});

    const response = await PATCH(patchRequest({ display_name: null }, `__session=${refreshToken}`));
    expect(response.status).toBe(200);
    const body = (await response.json()) as { display_name: string };
    // Falls back to discord_username per §4.1's resolution order — the
    // inserted row has no discord_global_name either.
    expect(body.display_name).toBe("user_9");
  });

  it("only ever updates the caller's own row (no user_id field is accepted)", async () => {
    const victim = await insertUser("10");
    const attacker = await insertUser("11");
    const { refreshToken } = await createSession(attacker.id, {});

    await PATCH(
      patchRequest({ display_name: "Nova", user_id: victim.id }, `__session=${refreshToken}`)
    );

    const [victimRow] = await db.select().from(users).where(sql`${users.id} = ${victim.id}`);
    const [attackerRow] = await db.select().from(users).where(sql`${users.id} = ${attacker.id}`);
    expect(victimRow.displayName).toBeNull();
    expect(attackerRow.displayName).toBe("Nova");
  });
});
