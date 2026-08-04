import { sql } from "drizzle-orm";
import { NextRequest } from "next/server";
import { afterEach, beforeAll, describe, expect, it } from "vitest";

import { db } from "@/db/client";
import { channels, guilds, messages, users } from "@/db/schema";
import { createChannel, createGuild } from "@/lib/internal/catalog";
import { toUserWireId } from "@/lib/internal/wireIds";

import { POST as createMessageRoute } from "../route";
import { GET } from "./route";

const MESSAGES_URL = "http://internal/internal/messages";
const LAST_SEQUENCE_URL = "http://internal/internal/messages/last-sequence";
const SECRET_HEADERS = { "x-internal-secret": "test-internal-secret", "content-type": "application/json" };

beforeAll(() => {
  process.env.INTERNAL_API_SHARED_SECRET = "test-internal-secret";
});

afterEach(async () => {
  await db.execute(sql`TRUNCATE TABLE ${messages}, ${channels}, ${guilds}, ${users} RESTART IDENTITY CASCADE`);
});

async function insertUser(discordId: string) {
  const [user] = await db.insert(users).values({ discordId, discordUsername: `user_${discordId}` }).returning();
  return user;
}

describe("GET /internal/messages/last-sequence", () => {
  it("returns 401 without the shared secret", async () => {
    const request = new NextRequest(LAST_SEQUENCE_URL);
    expect((await GET(request)).status).toBe(401);
  });

  it("returns null for all three when no messages exist", async () => {
    const response = await GET(new NextRequest(LAST_SEQUENCE_URL, { headers: SECRET_HEADERS }));
    const body = (await response.json()) as { lobby_seq: number | null; channel_seq: number | null; dm_seq: number | null };
    expect(body).toEqual({ lobby_seq: null, channel_seq: null, dm_seq: null });
  });

  it("returns the max seq per scope, independent of each other", async () => {
    const owner = await insertUser("1");
    const peer = await insertUser("2");
    const guild = await createGuild("My Guild", toUserWireId(owner.id));
    const channel = await createChannel(guild.guild_id, "general", "TEXT");

    for (const seq of [1, 2, 5]) {
      await createMessageRoute(
        new NextRequest(MESSAGES_URL, {
          method: "POST",
          headers: SECRET_HEADERS,
          body: JSON.stringify({ channel_id: null, user_id: toUserWireId(owner.id), content: `m${seq}`, seq })
        })
      );
    }
    for (const seq of [1, 2]) {
      await createMessageRoute(
        new NextRequest(MESSAGES_URL, {
          method: "POST",
          headers: SECRET_HEADERS,
          body: JSON.stringify({
            channel_id: channel.channel_id,
            user_id: toUserWireId(owner.id),
            content: `c${seq}`,
            seq
          })
        })
      );
    }
    for (const seq of [1, 2, 3]) {
      await createMessageRoute(
        new NextRequest(MESSAGES_URL, {
          method: "POST",
          headers: SECRET_HEADERS,
          body: JSON.stringify({
            peer_id: toUserWireId(peer.id),
            user_id: toUserWireId(owner.id),
            content: `d${seq}`,
            seq
          })
        })
      );
    }

    const response = await GET(new NextRequest(LAST_SEQUENCE_URL, { headers: SECRET_HEADERS }));
    const body = (await response.json()) as { lobby_seq: number | null; channel_seq: number | null; dm_seq: number | null };
    expect(body).toEqual({ lobby_seq: 5, channel_seq: 2, dm_seq: 3 });
  });
});
