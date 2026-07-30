// Wire-shaped types (server -> client), matching shared/protocol/README.md
// exactly — field names stay snake_case here on purpose, mirroring the
// naming convention web/lib/internal/*.ts already uses for its own
// wire-shaped types. useGatewayConnection (web/hooks/) is the one place
// that converts these to the camelCase types below before any component
// sees them (docs/frontend-rebuild-plan.md, "Wire-message casing
// convention").

export interface WireWelcome {
  type: "WELCOME";
  server_version: string;
}

export interface WireIdentified {
  type: "IDENTIFIED";
  user_id: string;
  username: string;
}

export interface WireChatMessage {
  type: "CHAT_MESSAGE";
  message_id: number;
  timestamp: number;
  user_id: string;
  username: string;
  content: string;
}

export interface WireGuild {
  guild_id: string;
  name: string;
  owner_id: string;
}

export interface WireChannel {
  channel_id: string;
  name: string;
  channel_type: "TEXT" | "VOICE";
}

export interface WireGuildList {
  type: "GUILD_LIST";
  guilds: WireGuild[];
}

export interface WireGuildCreated extends WireGuild {
  type: "GUILD_CREATED";
}

export interface WireGuildJoined extends WireGuild {
  type: "GUILD_JOINED";
  channels: WireChannel[];
}

export interface WireMemberLeft {
  type: "MEMBER_LEFT";
  guild_id: string;
  user_id: string;
}

export interface WireGuildDeleted {
  type: "GUILD_DELETED";
  guild_id: string;
}

export interface WireChannelList {
  type: "CHANNEL_LIST";
  guild_id: string;
  channels: WireChannel[];
}

export interface WireChannelCreated extends WireChannel {
  type: "CHANNEL_CREATED";
  guild_id: string;
}

export interface WireChannelDeleted {
  type: "CHANNEL_DELETED";
  guild_id: string;
  channel_id: string;
}

export interface WireChannelJoined {
  type: "CHANNEL_JOINED";
  guild_id: string;
  channel_id: string;
}

export interface WireChannelLeft {
  type: "CHANNEL_LEFT";
  channel_id: string;
}

export interface WireChannelMessage {
  type: "CHANNEL_MESSAGE";
  channel_id: string;
  guild_id: string;
  message_id: number;
  timestamp: number;
  user_id: string;
  username: string;
  content: string;
}

export interface WireError {
  type: "ERROR";
  code: string;
  message: string;
}

// Every message type the server may push unprompted, i.e. everything
// gateway.ts's onMessage callback can receive. WELCOME/IDENTIFIED are
// handled inside gateway.ts itself (the HELLO/IDENTIFY handshake) but stay
// in this union since they still pass through onMessage.
export type WireInboundMessage =
  | WireWelcome
  | WireIdentified
  | WireChatMessage
  | WireGuildList
  | WireGuildCreated
  | WireGuildJoined
  | WireMemberLeft
  | WireGuildDeleted
  | WireChannelList
  | WireChannelCreated
  | WireChannelDeleted
  | WireChannelJoined
  | WireChannelLeft
  | WireChannelMessage
  | WireError;

// camelCase types components actually consume. useGatewayConnection
// (web/hooks/) is the only place that constructs these, via the mapper
// functions below, converting from the Wire* shapes above right as each
// message comes in — no component ever sees a snake_case field.

export interface Guild {
  guildId: string;
  name: string;
  ownerId: string;
}

export interface Channel {
  channelId: string;
  name: string;
  channelType: "TEXT" | "VOICE";
}

export interface ChatMessage {
  messageId: number;
  timestamp: number;
  userId: string;
  username: string;
  content: string;
}

export interface ChannelMessage {
  channelId: string;
  guildId: string;
  messageId: number;
  timestamp: number;
  userId: string;
  username: string;
  content: string;
}

export function mapGuild(wire: WireGuild): Guild {
  return { guildId: wire.guild_id, name: wire.name, ownerId: wire.owner_id };
}

export function mapChannel(wire: WireChannel): Channel {
  return { channelId: wire.channel_id, name: wire.name, channelType: wire.channel_type };
}

export function mapChatMessage(wire: WireChatMessage): ChatMessage {
  return {
    messageId: wire.message_id,
    timestamp: wire.timestamp,
    userId: wire.user_id,
    username: wire.username,
    content: wire.content
  };
}

export function mapChannelMessage(wire: WireChannelMessage): ChannelMessage {
  return {
    channelId: wire.channel_id,
    guildId: wire.guild_id,
    messageId: wire.message_id,
    timestamp: wire.timestamp,
    userId: wire.user_id,
    username: wire.username,
    content: wire.content
  };
}
