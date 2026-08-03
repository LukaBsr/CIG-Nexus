import type { WireInboundMessage } from "./types";

let ws: WebSocket | null = null;

export type ConnectionStatus = "connecting" | "connected" | "disconnected" | "unauthenticated" | "error";

interface SessionTokenResponse {
  token: string;
  expires_at: string;
}

// design doc §6, "Bridging the browser-held token to the WebSocket": the
// httpOnly refresh cookie authenticates this call automatically
// (credentials: "include"); the returned short-lived access JWT is what
// IDENTIFY actually sends. Returns null (not a thrown error) when the
// caller isn't logged in at all — that's an expected, common state here,
// not a failure to log.
async function fetchSessionToken(): Promise<string | null> {
  const response = await fetch("/api/auth/session-token", { credentials: "include" });
  if (!response.ok) {
    return null;
  }

  const data = (await response.json()) as SessionTokenResponse;
  return data.token;
}

export function connect(
  onMessage: (msg: WireInboundMessage) => void,
  onStatus: (status: ConnectionStatus) => void
): void {
  onStatus("connecting");

  void (async () => {
    const sessionToken = await fetchSessionToken();
    if (!sessionToken) {
      onStatus("unauthenticated");
      return;
    }

    const url = process.env.NEXT_PUBLIC_GATEWAY_URL || "ws://localhost:8080";

    ws = new WebSocket(url);

    ws.onopen = () => {
      onStatus("connected");
      ws?.send(
        JSON.stringify({
          type: "HELLO",
          version: "0.1",
          client: "web"
        })
      );
    };

    ws.onmessage = (event) => {
      try {
        const message = JSON.parse(event.data) as WireInboundMessage;

        if (message.type === "WELCOME") {
          ws?.send(
            JSON.stringify({
              type: "IDENTIFY",
              session_token: sessionToken
            })
          );
        }

        onMessage(message);
      } catch (error) {
        console.error("Failed to parse message:", error);
      }
    };

    ws.onclose = () => {
      onStatus("disconnected");
    };

    ws.onerror = () => {
      onStatus("error");
    };
  })();
}

function send(payload: Record<string, unknown>): void {
  if (!ws || ws.readyState !== WebSocket.OPEN) {
    console.error("WebSocket not connected");
    return;
  }

  ws.send(JSON.stringify(payload));
}

export function sendChatMessage(content: string): void {
  send({ type: "CHAT_MESSAGE", content: content });
}

export function createGuild(name: string, visibility: "open" | "application" | "private" = "open"): void {
  send({ type: "CREATE_GUILD", name: name, visibility: visibility });
}

export function listGuilds(): void {
  send({ type: "LIST_GUILDS" });
}

export function joinGuild(guildId: string): void {
  send({ type: "JOIN_GUILD", guild_id: guildId });
}

// docs/guilds/social-presence-design.md §1.9: the application-visibility
// counterpart to joinGuild() above — used instead of JOIN_GUILD when the
// target guild's visibility is "application" (JOIN_GUILD itself is
// rejected with GUILD_REQUIRES_APPROVAL for those).
export function requestJoin(guildId: string): void {
  send({ type: "REQUEST_JOIN", guild_id: guildId });
}

export function listJoinRequests(guildId: string): void {
  send({ type: "LIST_JOIN_REQUESTS", guild_id: guildId });
}

export function approveJoinRequest(guildId: string, userId: string): void {
  send({ type: "APPROVE_JOIN_REQUEST", guild_id: guildId, user_id: userId });
}

export function rejectJoinRequest(guildId: string, userId: string): void {
  send({ type: "REJECT_JOIN_REQUEST", guild_id: guildId, user_id: userId });
}

export function leaveGuild(guildId: string): void {
  send({ type: "LEAVE_GUILD", guild_id: guildId });
}

export function deleteGuild(guildId: string): void {
  send({ type: "DELETE_GUILD", guild_id: guildId });
}

export function listChannels(guildId: string): void {
  send({ type: "LIST_CHANNELS", guild_id: guildId });
}

// docs/guilds/social-presence-design.md §2.1: fetched alongside listChannels() so
// the client can gate permission-sensitive UI on the viewer's own
// role_rank (canCreateInvite/canCreateChannel are officer-or-above, not
// owner-only — see §2.2).
export function listMembers(guildId: string): void {
  send({ type: "LIST_MEMBERS", guild_id: guildId });
}

export function createChannel(
  guildId: string,
  name: string,
  channelType: "TEXT" | "VOICE"
): void {
  send({
    type: "CREATE_CHANNEL",
    guild_id: guildId,
    name: name,
    channel_type: channelType
  });
}

export function deleteChannel(guildId: string, channelId: string): void {
  send({ type: "DELETE_CHANNEL", guild_id: guildId, channel_id: channelId });
}

export function joinChannel(channelId: string): void {
  send({ type: "JOIN_CHANNEL", channel_id: channelId });
}

export function leaveChannel(): void {
  send({ type: "LEAVE_CHANNEL" });
}

export function sendChannelMessage(content: string): void {
  send({ type: "CHANNEL_MESSAGE", content: content });
}

// docs/guilds/social-presence-design.md §1.4/§6 step 6: maxUses/expiresInSeconds
// are the two fields the custom invite-creation UI exposes instead of a
// client hardcoding null/null — both stay optional (null = unlimited
// uses / never expires, §1.6's reusable-by-default recommendation).
export function createInvite(
  guildId: string,
  maxUses: number | null,
  expiresInSeconds: number | null
): void {
  send({
    type: "CREATE_INVITE",
    guild_id: guildId,
    max_uses: maxUses,
    expires_in_seconds: expiresInSeconds
  });
}
