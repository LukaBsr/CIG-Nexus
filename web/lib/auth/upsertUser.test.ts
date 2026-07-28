import { sql } from "drizzle-orm";
import { afterEach, describe, expect, it } from "vitest";

import { db } from "@/db/client";
import { users } from "@/db/schema";

import { upsertDiscordUser } from "./upsertUser";

afterEach(async () => {
  await db.execute(sql`TRUNCATE TABLE ${users} RESTART IDENTITY CASCADE`);
});

describe("upsertDiscordUser", () => {
  it("creates a new user on first login", async () => {
    const user = await upsertDiscordUser({
      id: "111",
      username: "alice",
      global_name: "Alice",
      avatar: "hash1"
    });

    expect(user.discordId).toBe("111");
    expect(user.discordUsername).toBe("alice");
    expect(user.discordGlobalName).toBe("Alice");
    expect(user.discordAvatarHash).toBe("hash1");
  });

  it("updates display fields on a repeat login instead of creating a duplicate", async () => {
    const first = await upsertDiscordUser({
      id: "222",
      username: "bob",
      global_name: "Bob",
      avatar: "hash-old"
    });

    const second = await upsertDiscordUser({
      id: "222",
      username: "bob_renamed",
      global_name: "Bobby",
      avatar: "hash-new"
    });

    expect(second.id).toBe(first.id);
    expect(second.discordUsername).toBe("bob_renamed");
    expect(second.discordAvatarHash).toBe("hash-new");

    const all = await db.select().from(users).where(sql`${users.discordId} = '222'`);
    expect(all).toHaveLength(1);
  });
});
