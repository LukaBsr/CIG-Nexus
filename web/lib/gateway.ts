let ws: WebSocket | null = null;

export function connect(
  onMessage: (msg: any) => void,
  onStatus: (status: string) => void
): void {
  const url =
    process.env.NEXT_PUBLIC_GATEWAY_URL || "ws://localhost:8080"

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
      const message = JSON.parse(event.data);

      if (message.type === "WELCOME") {
        ws?.send(
          JSON.stringify({
            type: "IDENTIFY",
            username: "web_user"
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

export function createGuild(name: string): void {
  send({ type: "CREATE_GUILD", name: name });
}

export function listGuilds(): void {
  send({ type: "LIST_GUILDS" });
}

export function joinGuild(guildId: string): void {
  send({ type: "JOIN_GUILD", guild_id: guildId });
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
