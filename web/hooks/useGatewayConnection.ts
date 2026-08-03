"use client";

import { useEffect, useRef, useState } from "react";

import {
  approveJoinRequest as sendApproveJoinRequest,
  connect,
  type ConnectionStatus,
  createChannel as sendCreateChannel,
  createGuild as sendCreateGuild,
  createInvite as sendCreateInvite,
  joinChannel as sendJoinChannel,
  joinGuild as sendJoinGuild,
  listChannels,
  listGuilds,
  listJoinRequests as sendListJoinRequests,
  listMembers,
  rejectJoinRequest as sendRejectJoinRequest,
  requestJoin as sendRequestJoin,
  sendChannelMessage as sendChannelMessageWire,
  sendChatMessage as sendChatMessageWire
} from "@/lib/gateway";
import {
  mapChannel,
  mapChannelMessage,
  mapChatMessage,
  mapGuild,
  mapInvite,
  mapJoinRequest,
  mapMember,
  type Channel,
  type ChannelMessage,
  type ChatMessage,
  type Guild,
  type Invite,
  type JoinRequest,
  type Member
} from "@/lib/types";

export interface UseGatewayConnectionResult {
  status: ConnectionStatus;
  myUserId: string | null;
  chatMessages: ChatMessage[];
  guilds: Guild[];
  myGuildIds: Set<string>;
  myPendingJoinRequestGuildIds: Set<string>;
  activeGuildId: string | null;
  channels: Channel[];
  activeChannelId: string | null;
  channelMessages: ChannelMessage[];
  members: Member[];
  onlineUserIds: Set<string>;
  joinRequests: JoinRequest[];
  lastError: string | null;
  clearError: () => void;
  lastCreatedInvite: Invite | null;
  clearLastCreatedInvite: () => void;
  sendChatMessage: (content: string) => void;
  createGuild: (name: string, visibility?: "open" | "application" | "private") => void;
  joinGuild: (guildId: string) => void;
  requestJoin: (guildId: string) => void;
  selectGuild: (guildId: string) => void;
  createChannel: (guildId: string, name: string, channelType: "TEXT" | "VOICE") => void;
  joinChannel: (channelId: string) => void;
  sendChannelMessage: (content: string) => void;
  createInvite: (guildId: string, maxUses: number | null, expiresInSeconds: number | null) => void;
  listJoinRequests: (guildId: string) => void;
  approveJoinRequest: (guildId: string, userId: string) => void;
  rejectJoinRequest: (guildId: string, userId: string) => void;
}

// Owns the WebSocket connection's entire lifecycle: opening it
// (lib/gateway.ts's connect()), routing every inbound message by type, and
// converting wire (snake_case) fields to camelCase before they ever reach a
// component (docs/frontend-rebuild-plan.md, "Wire-message casing
// convention"). Components read only the camelCase state and action
// dispatchers this hook returns — never a raw wire message.
export function useGatewayConnection(): UseGatewayConnectionResult {
  const [status, setStatus] = useState<ConnectionStatus>("disconnected");
  const [myUserId, setMyUserId] = useState<string | null>(null);
  const [chatMessages, setChatMessages] = useState<ChatMessage[]>([]);

  const [guilds, setGuilds] = useState<Guild[]>([]);
  const [myGuildIds, setMyGuildIds] = useState<Set<string>>(new Set());
  const [myPendingJoinRequestGuildIds, setMyPendingJoinRequestGuildIds] = useState<Set<string>>(
    new Set()
  );
  const [activeGuildId, setActiveGuildId] = useState<string | null>(null);

  const [channels, setChannels] = useState<Channel[]>([]);
  const [activeChannelId, setActiveChannelId] = useState<string | null>(null);
  const [channelMessages, setChannelMessages] = useState<ChannelMessage[]>([]);
  const [members, setMembers] = useState<Member[]>([]);
  // docs/guilds/social-presence-design.md §3.2: lobby-wide, not per-guild —
  // every identified connection's user_id currently online, intersected
  // against `members` client-side wherever a per-guild view is needed.
  const [onlineUserIds, setOnlineUserIds] = useState<Set<string>>(new Set());
  const [joinRequests, setJoinRequests] = useState<JoinRequest[]>([]);

  const [lastError, setLastError] = useState<string | null>(null);
  const [lastCreatedInvite, setLastCreatedInvite] = useState<Invite | null>(null);

  // onMessage is captured once by connect() in the effect below, so it can't
  // see later state directly (stale closure) — these refs mirror the state
  // it needs to branch on synchronously.
  const myUserIdRef = useRef<string | null>(null);
  const activeGuildIdRef = useRef<string | null>(null);
  const activeChannelIdRef = useRef<string | null>(null);

  useEffect(() => {
    myUserIdRef.current = myUserId;
  }, [myUserId]);

  useEffect(() => {
    activeGuildIdRef.current = activeGuildId;
  }, [activeGuildId]);

  useEffect(() => {
    activeChannelIdRef.current = activeChannelId;
  }, [activeChannelId]);

  useEffect(() => {
    connect(
      (msg) => {
        switch (msg.type) {
          case "IDENTIFIED":
            setMyUserId(msg.user_id);
            listGuilds();
            break;

          case "CHAT_MESSAGE":
            setChatMessages((prev) => [...prev, mapChatMessage(msg)]);
            break;

          case "GUILD_LIST":
            setGuilds(msg.guilds.map(mapGuild));
            break;

          case "GUILD_CREATED": {
            const guild = mapGuild(msg);
            setGuilds((prev) => [...prev, guild]);
            setMyGuildIds((prev) => new Set(prev).add(guild.guildId));
            setActiveGuildId(guild.guildId);
            setChannels([]);
            setActiveChannelId(null);
            setChannelMessages([]);
            setMembers([]);
            setJoinRequests([]);
            listMembers(guild.guildId);
            break;
          }

          case "GUILD_JOINED": {
            const guild = mapGuild(msg);
            setGuilds((prev) =>
              prev.some((g) => g.guildId === guild.guildId) ? prev : [...prev, guild]
            );
            setMyGuildIds((prev) => new Set(prev).add(guild.guildId));
            setMyPendingJoinRequestGuildIds((prev) => {
              if (!prev.has(guild.guildId)) return prev;
              const next = new Set(prev);
              next.delete(guild.guildId);
              return next;
            });
            setActiveGuildId(guild.guildId);
            setChannels(msg.channels.map(mapChannel));
            setActiveChannelId(null);
            setChannelMessages([]);
            setMembers([]);
            setJoinRequests([]);
            listMembers(guild.guildId);
            break;
          }

          case "MEMBER_LEFT":
            if (msg.user_id === myUserIdRef.current) {
              setMyGuildIds((prev) => {
                const next = new Set(prev);
                next.delete(msg.guild_id);
                return next;
              });
              if (msg.guild_id === activeGuildIdRef.current) {
                setActiveGuildId(null);
                setChannels([]);
                setActiveChannelId(null);
                setChannelMessages([]);
                setMembers([]);
                setJoinRequests([]);
              }
            } else if (msg.guild_id === activeGuildIdRef.current) {
              setMembers((prev) => prev.filter((m) => m.userId !== msg.user_id));
            }
            break;

          case "GUILD_DELETED":
            setGuilds((prev) => prev.filter((g) => g.guildId !== msg.guild_id));
            setMyGuildIds((prev) => {
              const next = new Set(prev);
              next.delete(msg.guild_id);
              return next;
            });
            if (msg.guild_id === activeGuildIdRef.current) {
              setActiveGuildId(null);
              setChannels([]);
              setActiveChannelId(null);
              setChannelMessages([]);
              setMembers([]);
              setJoinRequests([]);
            }
            break;

          case "CHANNEL_LIST":
            setChannels(msg.channels.map(mapChannel));
            break;

          case "CHANNEL_CREATED":
            if (msg.guild_id === activeGuildIdRef.current) {
              setChannels((prev) => [...prev, mapChannel(msg)]);
            }
            break;

          case "CHANNEL_DELETED":
            if (msg.guild_id === activeGuildIdRef.current) {
              setChannels((prev) => prev.filter((c) => c.channelId !== msg.channel_id));
            }
            if (msg.channel_id === activeChannelIdRef.current) {
              setActiveChannelId(null);
              setChannelMessages([]);
            }
            break;

          case "CHANNEL_JOINED":
            setActiveChannelId(msg.channel_id);
            setChannelMessages([]);
            break;

          case "CHANNEL_LEFT":
            setActiveChannelId(null);
            setChannelMessages([]);
            break;

          case "CHANNEL_MESSAGE":
            setChannelMessages((prev) => [...prev, mapChannelMessage(msg)]);
            break;

          case "INVITE_CREATED":
            setLastCreatedInvite(mapInvite(msg));
            break;

          case "MEMBER_LIST":
            if (msg.guild_id === activeGuildIdRef.current) {
              setMembers(msg.members.map(mapMember));
            }
            break;

          case "MEMBER_ROLE_UPDATED":
            if (msg.guild_id === activeGuildIdRef.current) {
              setMembers((prev) =>
                prev.map((m) =>
                  m.userId === msg.user_id ? { ...m, roleRank: msg.role_rank, roleLabel: msg.role_label } : m
                )
              );
            }
            break;

          case "PRESENCE_UPDATE":
            setOnlineUserIds((prev) => {
              const next = new Set(prev);
              if (msg.status === "online") {
                next.add(msg.user_id);
              } else {
                next.delete(msg.user_id);
              }
              return next;
            });
            break;

          case "JOIN_REQUESTED":
            setMyPendingJoinRequestGuildIds((prev) => new Set(prev).add(msg.guild_id));
            break;

          case "JOIN_REQUEST_RECEIVED":
            if (msg.guild_id === activeGuildIdRef.current) {
              setJoinRequests((prev) =>
                prev.some((r) => r.userId === msg.user_id)
                  ? prev
                  : [...prev, { userId: msg.user_id, username: msg.username, requestedAt: new Date().toISOString() }]
              );
            }
            break;

          case "JOIN_REQUEST_LIST":
            if (msg.guild_id === activeGuildIdRef.current) {
              setJoinRequests(msg.requests.map(mapJoinRequest));
            }
            break;

          case "JOIN_REQUEST_APPROVED":
            if (msg.guild_id === activeGuildIdRef.current) {
              setJoinRequests((prev) => prev.filter((r) => r.userId !== msg.user_id));
              // JOIN_REQUEST_APPROVED carries only guild_id/user_id, not
              // enough to construct a full Member locally — refetch, same
              // pattern selectGuild() already uses.
              listMembers(msg.guild_id);
            }
            break;

          case "JOIN_REQUEST_REJECTED":
            if (msg.guild_id === activeGuildIdRef.current) {
              setJoinRequests((prev) => prev.filter((r) => r.userId !== msg.user_id));
            }
            if (msg.user_id === myUserIdRef.current) {
              setMyPendingJoinRequestGuildIds((prev) => {
                if (!prev.has(msg.guild_id)) return prev;
                const next = new Set(prev);
                next.delete(msg.guild_id);
                return next;
              });
            }
            break;

          case "ERROR":
            setLastError(msg.message ?? msg.code ?? "Unknown error");
            break;

          default:
            break;
        }
      },
      (newStatus) => {
        setStatus(newStatus);
      }
    );
  }, []);

  return {
    status,
    myUserId,
    chatMessages,
    guilds,
    myGuildIds,
    myPendingJoinRequestGuildIds,
    activeGuildId,
    channels,
    activeChannelId,
    channelMessages,
    members,
    onlineUserIds,
    joinRequests,
    lastError,
    clearError: () => setLastError(null),
    lastCreatedInvite,
    clearLastCreatedInvite: () => setLastCreatedInvite(null),
    sendChatMessage: (content) => sendChatMessageWire(content),
    createGuild: (name, visibility) => sendCreateGuild(name, visibility),
    joinGuild: (guildId) => sendJoinGuild(guildId),
    requestJoin: (guildId) => sendRequestJoin(guildId),
    selectGuild: (guildId) => {
      setActiveGuildId(guildId);
      setMembers([]);
      setJoinRequests([]);
      listChannels(guildId);
      listMembers(guildId);
    },
    createChannel: (guildId, name, channelType) => sendCreateChannel(guildId, name, channelType),
    joinChannel: (channelId) => sendJoinChannel(channelId),
    sendChannelMessage: (content) => sendChannelMessageWire(content),
    createInvite: (guildId, maxUses, expiresInSeconds) => sendCreateInvite(guildId, maxUses, expiresInSeconds),
    listJoinRequests: (guildId) => sendListJoinRequests(guildId),
    approveJoinRequest: (guildId, userId) => sendApproveJoinRequest(guildId, userId),
    rejectJoinRequest: (guildId, userId) => sendRejectJoinRequest(guildId, userId)
  };
}
