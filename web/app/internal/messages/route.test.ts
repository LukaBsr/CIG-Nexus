import { sql } from "drizzle-orm";
import { NextRequest } from "next/server";
import { afterEach, beforeAll, describe, expect, it } from "vitest";

import { db } from "@/db/client";
import { channels, guilds, messages, users } from "@/db/schema";
import { createGuild, createChannel } from "@/lib/internal/catalog";
import { toUserWireId } from "@/lib/internal/wireIds";

import { GET, POST } from "./route";

const URL = "http://internal/internal/messages";
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

function postRequest(body: unknown): NextRequest {
  return new NextRequest(URL, { method: "POST", headers: SECRET_HEADERS, body: JSON.stringify(body) });
}

function getRequest(query: string): NextRequest {
  return new NextRequest(`${URL}${query}`, { headers: SECRET_HEADERS });
}

describe("POST /internal/messages", () => {
  it("returns 401 without the shared secret", async () => {
    const request = new NextRequest(URL, { method: "POST", body: "{}" });
    expect((await POST(request)).status).toBe(401);
  });

  it("creates a lobby message with a null channel_id", async () => {
    const user = await insertUser("1");
    const response = await POST(
      postRequest({ channel_id: null, user_id: toUserWireId(user.id), content: "hello lobby", seq: 1 })
    );
    expect(response.status).toBe(201);
    const body = (await response.json()) as { channel_id: string | null; message_id: number };
    expect(body.channel_id).toBeNull();
    expect(body.message_id).toBe(1);
  });

  it("creates a channel message", async () => {
    const owner = await insertUser("2");
    const guild = await createGuild("My Guild", toUserWireId(owner.id));
    const channel = await createChannel(guild.guild_id, "general", "TEXT");

    const response = await POST(
      postRequest({ channel_id: channel.channel_id, user_id: toUserWireId(owner.id), content: "hi", seq: 1 })
    );
    expect(response.status).toBe(201);
    const body = (await response.json()) as { channel_id: string | null };
    expect(body.channel_id).toBe(channel.channel_id);
  });

  it("returns 400 for a user_id that doesn't reference a real user", async () => {
    const response = await POST(
      postRequest({ channel_id: null, user_id: "u_00000000-0000-0000-0000-000000000000", content: "hi", seq: 1 })
    );
    expect(response.status).toBe(400);
  });

  it("returns 400 for a malformed channel_id", async () => {
    const user = await insertUser("3");
    const response = await POST(
      postRequest({ channel_id: "not-a-wire-id", user_id: toUserWireId(user.id), content: "hi", seq: 1 })
    );
    expect(response.status).toBe(400);
  });

  it("returns 400 for content over 500 characters", async () => {
    const user = await insertUser("4");
    const response = await POST(
      postRequest({ channel_id: null, user_id: toUserWireId(user.id), content: "x".repeat(501), seq: 1 })
    );
    expect(response.status).toBe(400);
  });

  it("returns 400 for a duplicate seq in the same scope", async () => {
    const user = await insertUser("5");
    await POST(postRequest({ channel_id: null, user_id: toUserWireId(user.id), content: "one", seq: 1 }));
    const response = await POST(
      postRequest({ channel_id: null, user_id: toUserWireId(user.id), content: "two", seq: 1 })
    );
    expect(response.status).toBe(400);
  });

  it("returns 400 when seq is missing or not an integer", async () => {
    const user = await insertUser("6");
    const response = await POST(postRequest({ channel_id: null, user_id: toUserWireId(user.id), content: "hi" }));
    expect(response.status).toBe(400);
  });
});

describe("GET /internal/messages", () => {
  it("returns 401 without the shared secret", async () => {
    const request = new NextRequest(URL);
    expect((await GET(request)).status).toBe(401);
  });

  it("returns lobby history in chronological order with has_more", async () => {
    const user = await insertUser("7");
    for (let seq = 1; seq <= 3; seq += 1) {
      await POST(postRequest({ channel_id: null, user_id: toUserWireId(user.id), content: `msg-${seq}`, seq }));
    }

    const response = await GET(getRequest("?limit=2"));
    expect(response.status).toBe(200);
    const body = (await response.json()) as { messages: { content: string }[]; has_more: boolean };
    expect(body.messages.map((m) => m.content)).toEqual(["msg-2", "msg-3"]);
    expect(body.has_more).toBe(true);
  });

  it("pages further back with before_seq", async () => {
    const user = await insertUser("8");
    for (let seq = 1; seq <= 3; seq += 1) {
      await POST(postRequest({ channel_id: null, user_id: toUserWireId(user.id), content: `msg-${seq}`, seq }));
    }

    const response = await GET(getRequest("?limit=2&before_seq=2"));
    const body = (await response.json()) as { messages: { content: string }[]; has_more: boolean };
    expect(body.messages.map((m) => m.content)).toEqual(["msg-1"]);
    expect(body.has_more).toBe(false);
  });

  it("scopes to a specific channel and excludes lobby messages", async () => {
    const owner = await insertUser("9");
    const guild = await createGuild("My Guild", toUserWireId(owner.id));
    const channel = await createChannel(guild.guild_id, "general", "TEXT");

    await POST(postRequest({ channel_id: null, user_id: toUserWireId(owner.id), content: "lobby", seq: 1 }));
    await POST(
      postRequest({ channel_id: channel.channel_id, user_id: toUserWireId(owner.id), content: "channel", seq: 1 })
    );

    const response = await GET(getRequest(`?channel_id=${channel.channel_id}`));
    const body = (await response.json()) as { messages: { content: string }[] };
    expect(body.messages.map((m) => m.content)).toEqual(["channel"]);
  });

  it("includes the resolved username", async () => {
    const user = await insertUser("10");
    await POST(postRequest({ channel_id: null, user_id: toUserWireId(user.id), content: "hi", seq: 1 }));

    const response = await GET(getRequest(""));
    const body = (await response.json()) as { messages: { username: string }[] };
    expect(body.messages[0]?.username).toBe("user_10");
  });

  it("returns 400 for a malformed channel_id", async () => {
    const response = await GET(getRequest("?channel_id=not-a-wire-id"));
    expect(response.status).toBe(400);
  });

  it("returns 400 for an out-of-range limit", async () => {
    const response = await GET(getRequest("?limit=1000"));
    expect(response.status).toBe(400);
  });
});
