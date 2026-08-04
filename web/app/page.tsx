"use client";

import { useEffect, useState } from "react";

import { Header } from "@/components/Header";
import { InvitePopover } from "@/components/InvitePopover";
import { JoinRequestInbox } from "@/components/JoinRequestInbox";
import { LandingView } from "@/components/LandingView";
import { MemberList } from "@/components/MemberList";
import { MessageList } from "@/components/MessageList";
import { SettingsModal } from "@/components/SettingsModal";
import { TabButton, TabGroup } from "@/components/TabGroup";
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
    rejectJoinRequest
  } = useGatewayConnection();

  const [view, setView] = useState<"lobby" | "guilds">("lobby");
  const [isSettingsOpen, setIsSettingsOpen] = useState(false);
  const [input, setInput] = useState("");
  const [guildNameInput, setGuildNameInput] = useState("");
  const [isCreatingChannel, setIsCreatingChannel] = useState(false);
  const [channelNameInput, setChannelNameInput] = useState("");
  const [channelMessageInput, setChannelMessageInput] = useState("");

  const handleSend = () => {
    if (input.trim()) {
      sendChatMessage(input);
      setInput("");
    }
  };

  const handleCreateGuild = () => {
    if (guildNameInput.trim()) {
      createGuild(guildNameInput.trim());
      setGuildNameInput("");
    }
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
    <div className="flex h-screen flex-col bg-ink text-ivory">
      <Header status={status} onOpenSettings={() => setIsSettingsOpen(true)} />
      {isSettingsOpen && <SettingsModal userId={myUserId} onClose={() => setIsSettingsOpen(false)} />}

      <div className="flex shrink-0 items-center justify-between gap-3 border-b border-slate/20 px-5 py-2.5">
        <TabGroup>
          <TabButton active={view === "lobby"} onClick={() => setView("lobby")}>
            Lobby
          </TabButton>
          <TabButton active={view === "guilds"} onClick={() => setView("guilds")}>
            Guilds
          </TabButton>
        </TabGroup>

        {lastError && (
          <div className="flex min-w-0 flex-1 items-center justify-between gap-2 rounded-md border border-red-400/30 bg-red-400/10 px-3 py-1.5 font-mono text-xs text-red-300">
            <span className="truncate">{lastError}</span>
            <button onClick={clearError} className="shrink-0 font-semibold text-red-300/70 hover:text-red-300">
              &times;
            </button>
          </div>
        )}
      </div>

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

      {view === "guilds" && (
        <div className="flex flex-1 overflow-hidden">
          <aside className="flex w-60 shrink-0 flex-col overflow-y-auto border-r border-slate/20 p-3">
            <h2 className="mb-2 px-1 font-mono text-xs font-semibold tracking-wider text-ivory/40 uppercase">
              Guilds
            </h2>

            {guilds.length === 0 ? (
              <p className="px-1 font-mono text-xs text-ivory/40">No guilds yet — create one below.</p>
            ) : (
              <ul className="flex list-none flex-col gap-0.5 p-0">
                {guilds.map((g) => {
                  const isMember = myGuildIds.has(g.guildId);
                  const isPending = myPendingJoinRequestGuildIds.has(g.guildId);
                  return (
                    <li
                      key={g.guildId}
                      className="flex items-center justify-between gap-2 rounded-md px-1.5 py-1 transition-colors hover:bg-surface/60"
                    >
                      {isMember ? (
                        <button
                          onClick={() => selectGuild(g.guildId)}
                          className={`min-w-0 flex-1 truncate text-left font-mono text-sm transition-colors ${
                            g.guildId === activeGuildId
                              ? "font-semibold text-teal"
                              : "text-ivory/80 hover:text-ivory"
                          }`}
                        >
                          {g.name}
                        </button>
                      ) : (
                        <>
                          <span className="min-w-0 flex-1 truncate font-mono text-sm text-ivory/60">
                            {g.name}
                          </span>
                          {isPending ? (
                            <span className="shrink-0 rounded-full border border-slate/40 px-2.5 py-1 font-mono text-xs text-ivory/40">
                              Requested
                            </span>
                          ) : (
                            <button
                              onClick={() =>
                                g.visibility === "application" ? requestJoin(g.guildId) : joinGuild(g.guildId)
                              }
                              className="shrink-0 rounded-full bg-teal/15 px-2.5 py-1 font-mono text-xs font-semibold text-teal transition-colors hover:bg-teal/25"
                            >
                              {g.visibility === "application" ? "Request" : "Join"}
                            </button>
                          )}
                        </>
                      )}
                    </li>
                  );
                })}
              </ul>
            )}

            <div className="mt-3">
              <TextInputWithSubmit
                value={guildNameInput}
                onChange={setGuildNameInput}
                onSubmit={handleCreateGuild}
                placeholder="New guild name..."
                submitLabel="Create"
              />
            </div>
          </aside>

          <section className="flex flex-1 flex-col overflow-hidden">
            {!activeGuild ? (
              <div className="flex flex-1 items-center justify-center">
                <p className="font-mono text-sm text-ivory/40">Select or create a guild to get started.</p>
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

                  <MemberList members={members} onlineUserIds={onlineUserIds} />
                </div>
              </>
            )}
          </section>
        </div>
      )}
    </div>
  );
}
