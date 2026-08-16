"use client";

import { useEffect, useState } from "react";

import { FriendsView } from "@/components/FriendsView";
import { GuildRail } from "@/components/GuildRail";
import { Header } from "@/components/Header";
import { InvitePopover } from "@/components/InvitePopover";
import { JoinRequestInbox } from "@/components/JoinRequestInbox";
import { LandingView } from "@/components/LandingView";
import { MemberList } from "@/components/MemberList";
import { MessageList } from "@/components/MessageList";
import { SettingsModal } from "@/components/SettingsModal";
import { TextInputWithSubmit } from "@/components/TextInputWithSubmit";
import { useGatewayConnection } from "@/hooks/useGatewayConnection";
import { OFFICER_RANK } from "@/lib/roles";

function PlusIcon() {
  return (
    <svg
      width="12"
      height="12"
      viewBox="0 0 12 12"
      fill="none"
      stroke="currentColor"
      strokeWidth="1.8"
      strokeLinecap="round"
    >
      <path d="M6 1.5v9M1.5 6h9" />
    </svg>
  );
}

export default function Home() {
  const {
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
    clearError,
    lastCreatedInvite,
    clearLastCreatedInvite,
    sendChatMessage,
    createGuild,
    joinGuild,
    requestJoin,
    selectGuild,
    createChannel,
    joinChannel,
    sendChannelMessage,
    createInvite,
    listJoinRequests,
    approveJoinRequest,
    rejectJoinRequest,
    friends,
    incomingFriendRequests,
    outgoingFriendRequests,
    friendCode,
    addFriendByCode,
    regenerateFriendCode,
    acceptFriendRequest,
    rejectFriendRequest,
    cancelFriendRequest,
    blockUser,
    blockedUsers,
    unblockUser,
    dmConversations,
    activeDmPeerId,
    dmMessages,
    openDm,
    sendDm
  } = useGatewayConnection();

  const [view, setView] = useState<"lobby" | "guild" | "friends">("lobby");
  const [isSettingsOpen, setIsSettingsOpen] = useState(false);
  const [input, setInput] = useState("");
  const [isCreatingChannel, setIsCreatingChannel] = useState(false);
  const [channelNameInput, setChannelNameInput] = useState("");
  const [channelMessageInput, setChannelMessageInput] = useState("");
  const [dmInput, setDmInput] = useState("");

  const handleSend = () => {
    if (input.trim()) {
      sendChatMessage(input);
      setInput("");
    }
  };

  const handleSelectGuild = (guildId: string) => {
    selectGuild(guildId);
    setView("guild");
  };

  const handleCreateChannel = () => {
    if (activeGuildId && channelNameInput.trim()) {
      createChannel(activeGuildId, channelNameInput.trim(), "TEXT");
      setChannelNameInput("");
      setIsCreatingChannel(false);
    }
  };

  const handleSendChannelMessage = () => {
    if (channelMessageInput.trim()) {
      sendChannelMessage(channelMessageInput);
      setChannelMessageInput("");
    }
  };

  const activeGuild = guilds.find((g) => g.guildId === activeGuildId) ?? null;
  // canCreateChannel/canCreateInvite are officer-or-above, not owner-only
  // (docs/guilds/social-presence-design.md §2.2) — gate on the viewer's own
  // role_rank in the active guild's roster, not guild ownership.
  const myMembership = members.find((m) => m.userId === myUserId) ?? null;
  const isOfficerOrAbove = !!myMembership && myMembership.roleRank >= OFFICER_RANK;

  // Officers get the pending-request count refreshed whenever they switch
  // into a guild they can act on — mirrors how listMembers()/listChannels()
  // already refetch on guild switch inside the hook, just gated on role
  // here since the hook itself doesn't know about permissions.
  useEffect(() => {
    if (activeGuildId && isOfficerOrAbove) {
      listJoinRequests(activeGuildId);
    }
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [activeGuildId, isOfficerOrAbove]);

  if (status === "unauthenticated") {
    return <LandingView />;
  }

  return (
    <div className="flex h-screen bg-ink text-ivory">
      <GuildRail
        guilds={guilds}
        myGuildIds={myGuildIds}
        myPendingJoinRequestGuildIds={myPendingJoinRequestGuildIds}
        activeView={view}
        activeGuildId={activeGuildId}
        onSelectLobby={() => setView("lobby")}
        onSelectGuild={handleSelectGuild}
        onSelectFriends={() => setView("friends")}
        onJoinGuild={joinGuild}
        onRequestJoin={requestJoin}
        onCreateGuild={createGuild}
      />

      <div className="flex flex-1 flex-col overflow-hidden">
        <Header status={status} onOpenSettings={() => setIsSettingsOpen(true)} />
        {isSettingsOpen && (
          <SettingsModal
            userId={myUserId}
            onClose={() => setIsSettingsOpen(false)}
            blockedUsers={blockedUsers}
            onUnblock={unblockUser}
          />
        )}

        {lastError && (
          <div className="flex shrink-0 items-center justify-between gap-2 border-b border-slate/20 bg-red-400/10 px-5 py-1.5 font-mono text-xs text-red-300">
            <span className="truncate">{lastError}</span>
            <button onClick={clearError} className="shrink-0 font-semibold text-red-300/70 hover:text-red-300">
              &times;
            </button>
          </div>
        )}

        {view === "lobby" && (
          <div className="flex flex-1 flex-col overflow-hidden">
            <div className="shrink-0 px-5 py-2">
              <h2 className="font-mono text-sm font-semibold text-ivory">Global Lobby</h2>
              <p className="font-mono text-xs text-ivory/40">Broadcast to every connected client.</p>
            </div>

            <div className="flex-1 overflow-y-auto px-3">
              <div className="mx-auto max-w-3xl">
                <MessageList messages={chatMessages} emptyText="No messages yet — say hello." />
              </div>
            </div>

            <div className="shrink-0 border-t border-slate/20 p-3">
              <div className="mx-auto max-w-3xl">
                <TextInputWithSubmit
                  value={input}
                  onChange={setInput}
                  onSubmit={handleSend}
                  placeholder="Message the lobby..."
                  submitLabel="Send"
                  disabled={status !== "connected"}
                />
              </div>
            </div>
          </div>
        )}

        {view === "guild" && (
          <section className="flex flex-1 flex-col overflow-hidden">
            {!activeGuild ? (
              <div className="flex flex-1 items-center justify-center">
                <p className="font-mono text-sm text-ivory/40">
                  Select a guild from the rail, or use + to browse or create one.
                </p>
              </div>
            ) : (
              <>
                <div className="flex shrink-0 items-center justify-between gap-3 border-b border-slate/20 px-4 py-2">
                  <h2 className="min-w-0 truncate font-mono text-sm font-semibold text-ivory">
                    {activeGuild.name}
                  </h2>
                  {isOfficerOrAbove && (
                    <div className="flex shrink-0 items-center gap-1">
                      <button
                        onClick={() => setIsCreatingChannel((v) => !v)}
                        aria-label="Create channel"
                        title="Create channel"
                        className={`rounded-md p-1.5 transition-colors ${
                          isCreatingChannel ? "bg-surface text-teal" : "text-ivory/50 hover:text-ivory"
                        }`}
                      >
                        <PlusIcon />
                      </button>
                      <InvitePopover
                        onSubmit={(maxUses, expiresInSeconds) => {
                          if (activeGuildId) {
                            createInvite(activeGuildId, maxUses, expiresInSeconds);
                          }
                        }}
                        lastCreatedInvite={
                          lastCreatedInvite?.guildId === activeGuildId ? lastCreatedInvite : null
                        }
                        onClearLastCreatedInvite={clearLastCreatedInvite}
                      />
                      <JoinRequestInbox
                        requests={joinRequests}
                        onApprove={(userId) => activeGuildId && approveJoinRequest(activeGuildId, userId)}
                        onReject={(userId) => activeGuildId && rejectJoinRequest(activeGuildId, userId)}
                      />
                    </div>
                  )}
                </div>

                <div className="flex shrink-0 flex-wrap items-center gap-1.5 border-b border-slate/20 px-4 py-2">
                  {channels.length === 0 && !isCreatingChannel ? (
                    <p className="font-mono text-xs text-ivory/40">
                      {isOfficerOrAbove
                        ? "No channels yet — use + to create one."
                        : "This guild has no channels yet."}
                    </p>
                  ) : (
                    channels.map((c) => (
                      <button
                        key={c.channelId}
                        onClick={() => joinChannel(c.channelId)}
                        className={`rounded-full px-3 py-1 font-mono text-xs transition-colors ${
                          c.channelId === activeChannelId
                            ? "bg-teal font-semibold text-ink"
                            : "bg-surface text-ivory/70 hover:text-ivory"
                        }`}
                      >
                        #{c.name}
                      </button>
                    ))
                  )}
                  {isCreatingChannel && (
                    <div className="flex items-center gap-1">
                      <TextInputWithSubmit
                        value={channelNameInput}
                        onChange={setChannelNameInput}
                        onSubmit={handleCreateChannel}
                        placeholder="channel-name"
                        submitLabel="Add"
                      />
                    </div>
                  )}
                </div>

                <div className="flex flex-1 overflow-hidden">
                  <div className="flex flex-1 flex-col overflow-hidden">
                    {!activeChannelId ? (
                      <div className="flex flex-1 items-center justify-center">
                        <p className="font-mono text-sm text-ivory/40">Select a channel to start chatting.</p>
                      </div>
                    ) : (
                      <>
                        <div className="flex-1 overflow-y-auto px-3 py-2">
                          <div className="mx-auto max-w-3xl">
                            <MessageList
                              messages={channelMessages}
                              emptyText="No messages in this channel yet..."
                            />
                          </div>
                        </div>

                        <div className="shrink-0 border-t border-slate/20 p-3">
                          <div className="mx-auto max-w-3xl">
                            <TextInputWithSubmit
                              value={channelMessageInput}
                              onChange={setChannelMessageInput}
                              onSubmit={handleSendChannelMessage}
                              placeholder="Type a message..."
                              submitLabel="Send"
                            />
                          </div>
                        </div>
                      </>
                    )}
                  </div>

                  <MemberList
                    members={members}
                    onlineUserIds={onlineUserIds}
                    myUserId={myUserId}
                    onBlock={blockUser}
                  />
                </div>
              </>
            )}
          </section>
        )}

        {view === "friends" && (
          <FriendsView
            friends={friends}
            incomingFriendRequests={incomingFriendRequests}
            outgoingFriendRequests={outgoingFriendRequests}
            friendCode={friendCode}
            onlineUserIds={onlineUserIds}
            onAddByCode={addFriendByCode}
            onRegenerateCode={regenerateFriendCode}
            onAcceptRequest={acceptFriendRequest}
            onRejectRequest={rejectFriendRequest}
            onCancelRequest={cancelFriendRequest}
            onBlock={blockUser}
            dmConversations={dmConversations}
            activeDmPeerId={activeDmPeerId}
            dmMessages={dmMessages}
            onOpenDm={openDm}
            onSendDm={sendDm}
            dmInput={dmInput}
            onDmInputChange={setDmInput}
          />
        )}
      </div>
    </div>
  );
}
