"use client";

import { useEffect, useRef, useState } from "react";

import {
  acceptFriendRequest as sendAcceptFriendRequest,
  addFriendByCode as sendAddFriendByCode,
  approveJoinRequest as sendApproveJoinRequest,
  blockUser as sendBlockUser,
  cancelFriendRequest as sendCancelFriendRequest,
  connect,
  type ConnectionStatus,
  createChannel as sendCreateChannel,
  createGuild as sendCreateGuild,
  createInvite as sendCreateInvite,
  fetchDmHistory as sendFetchDmHistory,
  fetchFriendCode as sendFetchFriendCode,
  joinChannel as sendJoinChannel,
  joinGuild as sendJoinGuild,
  listBlocks as sendListBlocks,
  listChannels,
  listDmConversations as sendListDmConversations,
  listFriendRequests as sendListFriendRequests,
  listFriends as sendListFriends,
  listGuilds,
  listJoinRequests as sendListJoinRequests,
  listMembers,
  regenerateFriendCode as sendRegenerateFriendCode,
  rejectFriendRequest as sendRejectFriendRequest,
  rejectJoinRequest as sendRejectJoinRequest,
  removeFriend as sendRemoveFriend,
  requestJoin as sendRequestJoin,
  sendChannelMessage as sendChannelMessageWire,
  sendChatMessage as sendChatMessageWire,
  sendDm as sendDmWire,
  sendFriendRequest as sendSendFriendRequest,
  unblockUser as sendUnblockUser
} from "@/lib/gateway";
import {
  mapBlock,
  mapChannel,
  mapChannelMessage,
  mapChatMessage,
  mapDmConversation,
  mapDmMessage,
  mapFriend,
  mapFriendRequest,
  mapGuild,
  mapHistoryMessageToDmMessage,
  mapInvite,
  mapJoinRequest,
  mapMember,
  type Block,
  type Channel,
  type ChannelMessage,
  type ChatMessage,
  type DmConversation,
  type DmMessage,
  type Friend,
  type FriendRequest,
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
  friends: Friend[];
  incomingFriendRequests: FriendRequest[];
  outgoingFriendRequests: FriendRequest[];
  friendCode: string | null;
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
  sendFriendRequest: (userId: string) => void;
  addFriendByCode: (code: string) => void;
  acceptFriendRequest: (userId: string) => void;
  rejectFriendRequest: (userId: string) => void;
  cancelFriendRequest: (userId: string) => void;
  removeFriend: (userId: string) => void;
  listFriends: () => void;
  listFriendRequests: () => void;
  fetchFriendCode: () => void;
  regenerateFriendCode: () => void;
  blockedUsers: Block[];
  blockUser: (userId: string) => void;
  unblockUser: (userId: string) => void;
  listBlocks: () => void;
  dmConversations: DmConversation[];
  activeDmPeerId: string | null;
  dmMessages: DmMessage[];
  openDm: (peerId: string) => void;
  sendDm: (content: string) => void;
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

  // docs/social/friends-dms-design.md §1 — not guild-scoped, unlike
  // members/joinRequests above.
  const [friends, setFriends] = useState<Friend[]>([]);
  const [incomingFriendRequests, setIncomingFriendRequests] = useState<FriendRequest[]>([]);
  const [outgoingFriendRequests, setOutgoingFriendRequests] = useState<FriendRequest[]>([]);
  const [friendCode, setFriendCode] = useState<string | null>(null);
  const [blockedUsers, setBlockedUsers] = useState<Block[]>([]);

  // docs/social/friends-dms-design.md §3.5 — mirrors channelMessages/
  // activeChannelId's shape exactly: dmMessages holds only the active
  // thread's messages, since DM_MESSAGE (like CHANNEL_MESSAGE) implies
  // "at most one active thread" rather than carrying a conversation id.
  const [dmConversations, setDmConversations] = useState<DmConversation[]>([]);
  const [activeDmPeerId, setActiveDmPeerId] = useState<string | null>(null);
  const [dmMessages, setDmMessages] = useState<DmMessage[]>([]);

  const [lastError, setLastError] = useState<string | null>(null);
  const [lastCreatedInvite, setLastCreatedInvite] = useState<Invite | null>(null);

  // onMessage is captured once by connect() in the effect below, so it can't
  // see later state directly (stale closure) — these refs mirror the state
  // it needs to branch on synchronously.
  const myUserIdRef = useRef<string | null>(null);
  const activeGuildIdRef = useRef<string | null>(null);
  const activeChannelIdRef = useRef<string | null>(null);
  const activeDmPeerIdRef = useRef<string | null>(null);

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
    activeDmPeerIdRef.current = activeDmPeerId;
  }, [activeDmPeerId]);

  useEffect(() => {
    connect(
      (msg) => {
        switch (msg.type) {
          case "IDENTIFIED":
            setMyUserId(msg.user_id);
            listGuilds();
            // Eagerly fetched (not gated behind opening the Friends tab,
            // unlike guild join requests being gated behind officer rank
            // in a specific guild) so an incoming-request badge can show
            // immediately.
            sendListFriends();
            sendListFriendRequests();
            sendFetchFriendCode();
            sendListBlocks();
            sendListDmConversations();
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
                  : [
                      ...prev,
                      {
                        userId: msg.user_id,
                        username: msg.username,
                        requestedAt: new Date().toISOString(),
                        displayName: null,
                        avatarUrl: null
                      }
                    ]
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

          // docs/social/friends-dms-design.md §1.5: every one of these
          // acks/notifications carries only a bare user_id — not enough to
          // construct a full Friend/FriendRequest locally. Same precedent
          // JOIN_REQUEST_APPROVED above already established: refetch the
          // canonical list rather than hand-assembling a partial object.
          case "FRIEND_REQUEST_SENT":
          case "FRIEND_REQUEST_RECEIVED":
          case "FRIEND_REQUEST_REJECTED":
          case "FRIEND_REQUEST_CANCELED":
            sendListFriendRequests();
            break;

          case "FRIEND_ADDED":
            sendListFriends();
            sendListFriendRequests();
            break;

          case "FRIEND_REMOVED":
            sendListFriends();
            break;

          case "FRIEND_LIST":
            setFriends(msg.friends.map(mapFriend));
            break;

          case "FRIEND_REQUEST_LIST":
            setIncomingFriendRequests(msg.incoming.map(mapFriendRequest));
            setOutgoingFriendRequests(msg.outgoing.map(mapFriendRequest));
            break;

          case "FRIEND_CODE":
            setFriendCode(msg.code);
            break;

          // docs/social/friends-dms-design.md §2.3: BLOCK_USER's backend
          // transaction silently cancels any pending friend request and
          // removes an existing friendship, in the same transaction as the
          // block itself — but no FRIEND_REMOVED/request-cancellation
          // event is separately emitted for that side effect. Refetch
          // friends/requests here too, not just blocks, or local state
          // would silently go stale.
          case "USER_BLOCKED":
            sendListBlocks();
            sendListFriends();
            sendListFriendRequests();
            break;

          case "USER_UNBLOCKED":
            sendListBlocks();
            break;

          case "BLOCK_LIST":
            setBlockedUsers(msg.blocked.map(mapBlock));
            break;

          // docs/social/friends-dms-design.md §3.5: DM_MESSAGE carries no
          // conversation/recipient id at all, only the sender's user_id —
          // the protocol's "at most one active thread" model, same as
          // CHANNEL_MESSAGE's activeChannelId. A message from the
          // currently-open peer, or our own echo (trusted to belong to
          // whichever thread is active, since that's the only thread
          // sendDm() could have just targeted), gets appended; a message
          // for a different, non-active conversation just refreshes the
          // conversation list's ordering instead.
          case "DM_MESSAGE":
            if (msg.user_id === activeDmPeerIdRef.current || msg.user_id === myUserIdRef.current) {
              setDmMessages((prev) => [...prev, mapDmMessage(msg)]);
            } else {
              sendListDmConversations();
            }
            break;

          case "DM_CONVERSATION_LIST":
            setDmConversations(msg.conversations.map(mapDmConversation));
            break;

          case "MESSAGE_HISTORY":
            if (msg.peer_id !== undefined && msg.peer_id === activeDmPeerIdRef.current) {
              setDmMessages(msg.messages.map(mapHistoryMessageToDmMessage));
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
    rejectJoinRequest: (guildId, userId) => sendRejectJoinRequest(guildId, userId),
    friends,
    incomingFriendRequests,
    outgoingFriendRequests,
    friendCode,
    sendFriendRequest: (userId) => sendSendFriendRequest(userId),
    addFriendByCode: (code) => sendAddFriendByCode(code),
    acceptFriendRequest: (userId) => sendAcceptFriendRequest(userId),
    rejectFriendRequest: (userId) => sendRejectFriendRequest(userId),
    cancelFriendRequest: (userId) => sendCancelFriendRequest(userId),
    removeFriend: (userId) => sendRemoveFriend(userId),
    listFriends: () => sendListFriends(),
    listFriendRequests: () => sendListFriendRequests(),
    fetchFriendCode: () => sendFetchFriendCode(),
    regenerateFriendCode: () => sendRegenerateFriendCode(),
    blockedUsers,
    blockUser: (userId) => sendBlockUser(userId),
    unblockUser: (userId) => sendUnblockUser(userId),
    listBlocks: () => sendListBlocks(),
    dmConversations,
    activeDmPeerId,
    dmMessages,
    openDm: (peerId) => {
      setActiveDmPeerId(peerId);
      setDmMessages([]);
      sendFetchDmHistory(peerId, null, 50);
    },
    sendDm: (content) => {
      if (activeDmPeerIdRef.current) {
        sendDmWire(activeDmPeerIdRef.current, content);
      }
    }
  };
}
