"use client";

import { useState } from "react";

import { Header } from "@/components/Header";
import { LandingView } from "@/components/LandingView";
import { MessageList } from "@/components/MessageList";
import { TabButton, TabGroup } from "@/components/TabGroup";
import { TextInputWithSubmit } from "@/components/TextInputWithSubmit";
import { useGatewayConnection } from "@/hooks/useGatewayConnection";

export default function Home() {
  const {
    status,
    myUserId,
    chatMessages,
    guilds,
    myGuildIds,
    activeGuildId,
    channels,
    activeChannelId,
    channelMessages,
    lastError,
    clearError,
    sendChatMessage,
    createGuild,
    joinGuild,
    selectGuild,
    createChannel,
    joinChannel,
    sendChannelMessage
  } = useGatewayConnection();

  const [view, setView] = useState<"lobby" | "guilds">("lobby");
  const [input, setInput] = useState("");
  const [guildNameInput, setGuildNameInput] = useState("");
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
    }
  };

  const handleSendChannelMessage = () => {
    if (channelMessageInput.trim()) {
      sendChannelMessage(channelMessageInput);
      setChannelMessageInput("");
    }
  };

  const activeGuild = guilds.find((g) => g.guildId === activeGuildId) ?? null;
  const isOwner = !!activeGuild && !!myUserId && activeGuild.ownerId === myUserId;

  if (status === "unauthenticated") {
    return <LandingView />;
  }

  return (
    <div className="flex h-screen flex-col bg-ink text-ivory">
      <Header status={status} />

      <div className="flex shrink-0 flex-col gap-3 border-b border-slate/20 px-6 py-3">
        <TabGroup>
          <TabButton active={view === "lobby"} onClick={() => setView("lobby")}>
            Lobby
          </TabButton>
          <TabButton active={view === "guilds"} onClick={() => setView("guilds")}>
            Guilds
          </TabButton>
        </TabGroup>

        {lastError && (
          <div className="flex items-center justify-between rounded-md border border-red-400/30 bg-red-400/10 px-4 py-2 font-mono text-sm text-red-300">
            <span>{lastError}</span>
            <button onClick={clearError} className="ml-4 font-semibold text-red-300/70 hover:text-red-300">
              &times;
            </button>
          </div>
        )}
      </div>

      {view === "lobby" && (
        <div className="flex flex-1 flex-col overflow-hidden">
          <div className="shrink-0 px-6 py-3">
            <h2 className="font-mono text-sm font-semibold text-ivory">Global Lobby</h2>
            <p className="font-mono text-xs text-ivory/40">Broadcast to every connected client.</p>
          </div>

          <div className="flex-1 overflow-y-auto px-3">
            <div className="mx-auto max-w-3xl">
              <MessageList messages={chatMessages} emptyText="No messages yet — say hello." />
            </div>
          </div>

          <div className="shrink-0 border-t border-slate/20 p-4">
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
          <aside className="flex w-72 shrink-0 flex-col overflow-y-auto border-r border-slate/20 p-4">
            <h2 className="mb-3 font-mono text-xs font-semibold tracking-wider text-ivory/40 uppercase">
              Guilds
            </h2>

            {guilds.length === 0 ? (
              <p className="font-mono text-xs text-ivory/40">No guilds yet — create one below.</p>
            ) : (
              <ul className="flex list-none flex-col gap-1 p-0">
                {guilds.map((g) => {
                  const isMember = myGuildIds.has(g.guildId);
                  return (
                    <li
                      key={g.guildId}
                      className="flex items-center justify-between gap-2 rounded-md px-2 py-1.5 text-sm"
                    >
                      {isMember ? (
                        <button
                          onClick={() => selectGuild(g.guildId)}
                          className={`min-w-0 flex-1 truncate text-left transition-colors ${
                            g.guildId === activeGuildId
                              ? "font-semibold text-teal"
                              : "text-ivory/80 hover:text-ivory"
                          }`}
                        >
                          {g.name}
                        </button>
                      ) : (
                        <>
                          <span className="min-w-0 flex-1 truncate text-ivory/60">{g.name}</span>
                          <button
                            onClick={() => joinGuild(g.guildId)}
                            className="shrink-0 rounded-full bg-teal/15 px-2.5 py-1 font-mono text-xs font-semibold text-teal transition-colors hover:bg-teal/25"
                          >
                            Join
                          </button>
                        </>
                      )}
                    </li>
                  );
                })}
              </ul>
            )}

            <div className="mt-4">
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
                <div className="shrink-0 border-b border-slate/20 px-6 py-3">
                  <h2 className="truncate font-mono text-sm font-semibold text-ivory">{activeGuild.name}</h2>
                </div>

                <div className="flex shrink-0 flex-wrap gap-2 border-b border-slate/20 px-6 py-3">
                  {channels.length === 0 ? (
                    <p className="font-mono text-xs text-ivory/40">
                      {isOwner ? "No channels yet — create one below." : "This guild has no channels yet."}
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
                </div>

                {isOwner && (
                  <div className="shrink-0 border-b border-slate/20 px-6 py-3">
                    <TextInputWithSubmit
                      value={channelNameInput}
                      onChange={setChannelNameInput}
                      onSubmit={handleCreateChannel}
                      placeholder="New channel name..."
                      submitLabel="Create"
                    />
                  </div>
                )}

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

                      <div className="shrink-0 border-t border-slate/20 p-4">
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
              </>
            )}
          </section>
        </div>
      )}
    </div>
  );
}
