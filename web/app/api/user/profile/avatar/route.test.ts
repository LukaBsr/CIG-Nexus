import { sql } from "drizzle-orm";
import { rm } from "node:fs/promises";
import path from "node:path";
import { NextRequest } from "next/server";
import { afterEach, describe, expect, it } from "vitest";

import { db } from "@/db/client";
import { sessions, users } from "@/db/schema";
import { createSession } from "@/lib/auth/session";

import { DELETE, POST } from "./route";

const AVATAR_DIR = path.join(process.cwd(), "data", "avatars");

// Signature-only bytes — sniffImageType only inspects the leading magic
// number, so these don't need to be a fully valid decodable image.
const PNG_BYTES = Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0, 0, 0, 0]);

afterEach(async () => {
  await db.execute(sql`TRUNCATE TABLE ${sessions}, ${users} RESTART IDENTITY CASCADE`);
  await rm(AVATAR_DIR, { recursive: true, force: true });
});

async function insertUser(discordId: string, extra?: Partial<typeof users.$inferInsert>) {
  const [user] = await db
    .insert(users)
    .values({ discordId, discordUsername: `user_${discordId}`, ...extra })
    .returning();
  return user;
}

function requestWithForm(formData: FormData | null, cookie?: string, method = "POST"): NextRequest {
  return new NextRequest("http://localhost:3000/api/user/profile/avatar", {
    method,
    headers: new Headers(cookie ? { cookie } : {}),
    body: formData
  });
}

function requestWithoutBody(cookie?: string, method = "DELETE"): NextRequest {
  return new NextRequest("http://localhost:3000/api/user/profile/avatar", {
    method,
    headers: new Headers(cookie ? { cookie } : {})
  });
}

describe("POST /api/user/profile/avatar", () => {
  it("returns 401 when there is no refresh cookie", async () => {
    const response = await POST(requestWithForm(null));
    expect(response.status).toBe(401);
  });

  it("rejects a request with no avatar field", async () => {
    const user = await insertUser("1");
    const { refreshToken } = await createSession(user.id, {});

    const response = await POST(requestWithForm(new FormData(), `__session=${refreshToken}`));
    expect(response.status).toBe(400);
  });

  it("rejects a file that isn't a recognized image (spoofed extension/type)", async () => {
    const user = await insertUser("2");
    const { refreshToken } = await createSession(user.id, {});

    const form = new FormData();
    form.set("avatar", new Blob([Buffer.from("not an image")], { type: "image/png" }), "avatar.png");

    const response = await POST(requestWithForm(form, `__session=${refreshToken}`));
    expect(response.status).toBe(400);
  });

  it("rejects a file over the size cap", async () => {
    const user = await insertUser("3");
    const { refreshToken } = await createSession(user.id, {});

    const oversized = Buffer.concat([PNG_BYTES, Buffer.alloc(2 * 1024 * 1024)]);
    const form = new FormData();
    form.set("avatar", new Blob([oversized]), "avatar.png");

    const response = await POST(requestWithForm(form, `__session=${refreshToken}`));
    expect(response.status).toBe(400);
  });

  it("accepts a valid PNG, stores it under a random filename, and updates the account", async () => {
    const user = await insertUser("4");
    const { refreshToken } = await createSession(user.id, {});

    const form = new FormData();
    // Client-declared filename is deliberately adversarial (path
    // traversal attempt) — §5 checklist: the stored filename must be
    // server-generated regardless of what the client sends.
    form.set("avatar", new Blob([PNG_BYTES]), "../../etc/passwd.png");

    const response = await POST(requestWithForm(form, `__session=${refreshToken}`));
    expect(response.status).toBe(200);
    const body = (await response.json()) as { avatar_url: string };
    expect(body.avatar_url).toMatch(/^\/uploads\/avatars\/[0-9a-f]{32}\.png$/);

    const [row] = await db.select().from(users).where(sql`${users.id} = ${user.id}`);
    expect(row.customAvatarPath).toBe(body.avatar_url);
  });

  it("deletes the previous file when a new avatar replaces it", async () => {
    const user = await insertUser("5");
    const { refreshToken } = await createSession(user.id, {});

    const firstForm = new FormData();
    firstForm.set("avatar", new Blob([PNG_BYTES]), "first.png");
    const first = await POST(requestWithForm(firstForm, `__session=${refreshToken}`));
    const firstBody = (await first.json()) as { avatar_url: string };
    const firstPath = path.join(AVATAR_DIR, path.basename(firstBody.avatar_url));

    const secondForm = new FormData();
    secondForm.set("avatar", new Blob([PNG_BYTES]), "second.png");
    await POST(requestWithForm(secondForm, `__session=${refreshToken}`));

    // Best-effort async cleanup — give it a tick to complete.
    await new Promise((resolve) => setTimeout(resolve, 50));
    await expect(rm(firstPath, { force: false })).rejects.toThrow();
  });

  it("only ever updates the caller's own row", async () => {
    const victim = await insertUser("6");
    const attacker = await insertUser("7");
    const { refreshToken } = await createSession(attacker.id, {});

    const form = new FormData();
    form.set("avatar", new Blob([PNG_BYTES]), "avatar.png");
    await POST(requestWithForm(form, `__session=${refreshToken}`));

    const [victimRow] = await db.select().from(users).where(sql`${users.id} = ${victim.id}`);
    expect(victimRow.customAvatarPath).toBeNull();
  });
});

describe("DELETE /api/user/profile/avatar", () => {
  it("returns 401 when there is no refresh cookie", async () => {
    const response = await DELETE(requestWithoutBody());
    expect(response.status).toBe(401);
  });

  it("clears custom_avatar_path and returns the Discord-fallback avatar_url", async () => {
    const user = await insertUser("8", { discordAvatarHash: "abc123", customAvatarPath: "/uploads/avatars/old.png" });
    const { refreshToken } = await createSession(user.id, {});

    const response = await DELETE(requestWithoutBody(`__session=${refreshToken}`));
    expect(response.status).toBe(200);
    const body = (await response.json()) as { avatar_url: string | null };
    expect(body.avatar_url).toBe(`https://cdn.discordapp.com/avatars/${user.discordId}/abc123.png`);

    const [row] = await db.select().from(users).where(sql`${users.id} = ${user.id}`);
    expect(row.customAvatarPath).toBeNull();
  });
});
