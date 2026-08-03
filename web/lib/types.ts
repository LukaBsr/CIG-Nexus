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
  visibility: "open" | "application" | "private";
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

// docs/guilds/social-presence-design.md §1.4. Only INVITE_CREATED is modeled on
// the wire-inbound side today — the frontend's only invite-related UI is
// the creation form (§6 step 6's explicit scope); LIST_INVITES/REVOKE_INVITE
// aren't wired into the client yet.
export interface WireInviteCreated {
  type: "INVITE_CREATED";
  guild_id: string;
  code: string;
  max_uses: number | null;
  use_count: number;
  expires_at: string | null;
  created_at: string;
}

// docs/guilds/social-presence-design.md §2.1/§2.4. Fetched so the client can gate
// permission-sensitive UI (e.g. "can I create an invite/channel?") on the
// viewer's own role_rank instead of guild ownership — canCreateInvite/
// canCreateChannel are officer-or-above, not owner-only (§2.2).
export interface WireMember {
  user_id: string;
  username: string;
  role_rank: number;
  role_label: string;
  joined_at: string;
}

export interface WireMemberList {
  type: "MEMBER_LIST";
  guild_id: string;
  members: WireMember[];
}

export interface WireMemberRoleUpdated {
  type: "MEMBER_ROLE_UPDATED";
  guild_id: string;
  user_id: string;
  role_rank: number;
  role_label: string;
}

// docs/guilds/social-presence-design.md §3.2/§3.4: lobby-wide broadcast,
// not per-guild — the client intersects the globally-received online set
// against whichever guild's roster (LIST_MEMBERS) it already has locally.
export interface WirePresenceUpdate {
  type: "PRESENCE_UPDATE";
  user_id: string;
  status: "online" | "offline";
}

// docs/guilds/social-presence-design.md §1.9: the join-request flow for
// application-visibility guilds.
export interface WireJoinRequested {
  type: "JOIN_REQUESTED";
  guild_id: string;
}

export interface WireJoinRequestReceived {
  type: "JOIN_REQUEST_RECEIVED";
  guild_id: string;
  user_id: string;
  username: string;
}

export interface WireJoinRequestEntry {
  user_id: string;
  username: string;
  requested_at: string;
}

export interface WireJoinRequestList {
  type: "JOIN_REQUEST_LIST";
  guild_id: string;
  requests: WireJoinRequestEntry[];
}

export interface WireJoinRequestApproved {
  type: "JOIN_REQUEST_APPROVED";
  guild_id: string;
  user_id: string;
}

export interface WireJoinRequestRejected {
  type: "JOIN_REQUEST_REJECTED";
  guild_id: string;
  user_id: string;
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
  | WireInviteCreated
  | WireMemberList
  | WireMemberRoleUpdated
  | WirePresenceUpdate
  | WireJoinRequested
  | WireJoinRequestReceived
  | WireJoinRequestList
  | WireJoinRequestApproved
  | WireJoinRequestRejected
  | WireError;

// camelCase types components actually consume. useGatewayConnection
// (web/hooks/) is the only place that constructs these, via the mapper
// functions below, converting from the Wire* shapes above right as each
// message comes in — no component ever sees a snake_case field.

export interface Guild {
  guildId: string;
  name: string;
  ownerId: string;
  visibility: "open" | "application" | "private";
}

export interface Invite {
  guildId: string;
  code: string;
  maxUses: number | null;
  useCount: number;
  expiresAt: string | null;
  createdAt: string;
}

export interface JoinRequest {
  userId: string;
  username: string;
  requestedAt: string;
}

export interface Member {
  userId: string;
  username: string;
  roleRank: number;
  roleLabel: string;
  joinedAt: string;
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
  return { guildId: wire.guild_id, name: wire.name, ownerId: wire.owner_id, visibility: wire.visibility };
}

export function mapJoinRequest(wire: WireJoinRequestEntry): JoinRequest {
  return { userId: wire.user_id, username: wire.username, requestedAt: wire.requested_at };
}

export function mapInvite(wire: WireInviteCreated): Invite {
  return {
    guildId: wire.guild_id,
    code: wire.code,
    maxUses: wire.max_uses,
    useCount: wire.use_count,
    expiresAt: wire.expires_at,
    createdAt: wire.created_at
  };
}

export function mapMember(wire: WireMember): Member {
  return {
    userId: wire.user_id,
    username: wire.username,
    roleRank: wire.role_rank,
    roleLabel: wire.role_label,
    joinedAt: wire.joined_at
  };
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
