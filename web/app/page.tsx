"use client";

import { useEffect, useRef, useState } from "react";
import {
  connect,
  sendChatMessage,
  createGuild,
  listGuilds,
  joinGuild,
  listChannels,
  createChannel,
  joinChannel,
  sendChannelMessage
} from "../lib/gateway";

type Guild = {
  guild_id: string;
  name: string;
  owner_id: string;
};

type Channel = {
  channel_id: string;
  name: string;
  channel_type: "TEXT" | "VOICE";
};

type ChatMessage = {
  type: string;
  message_id: number;
  timestamp: number;
  user_id?: string;
  username?: string;
  from?: string;
  content: string;
};

type ChannelMessage = {
  channel_id: string;
  guild_id: string;
  message_id: number;
  timestamp: number;
  user_id: string;
  username: string;
  content: string;
};

export default function Home() {
  const [status, setStatus] = useState<string>("disconnected");
  const [chatMessages, setChatMessages] = useState<ChatMessage[]>([]);
  const [input, setInput] = useState<string>("");

  const [myUserId, setMyUserId] = useState<string | null>(null);
  const [view, setView] = useState<"chat" | "guilds">("chat");

  const [guilds, setGuilds] = useState<Guild[]>([]);
  const [myGuildIds, setMyGuildIds] = useState<Set<string>>(new Set());
  const [activeGuildId, setActiveGuildId] = useState<string | null>(null);

  const [channels, setChannels] = useState<Channel[]>([]);
  const [activeChannelId, setActiveChannelId] = useState<string | null>(null);
  const [channelMessages, setChannelMessages] = useState<ChannelMessage[]>([]);

  const [lastError, setLastError] = useState<string | null>(null);

  const [guildNameInput, setGuildNameInput] = useState<string>("");
  const [channelNameInput, setChannelNameInput] = useState<string>("");
  const [channelMessageInput, setChannelMessageInput] = useState<string>("");

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
            setChatMessages((prev) => [...prev, msg]);
            break;

          case "GUILD_LIST":
            setGuilds(msg.guilds ?? []);
            break;

          case "GUILD_CREATED":
            setGuilds((prev) => [
              ...prev,
              { guild_id: msg.guild_id, name: msg.name, owner_id: msg.owner_id }
            ]);
            setMyGuildIds((prev) => new Set(prev).add(msg.guild_id));
            setActiveGuildId(msg.guild_id);
            setChannels([]);
            setActiveChannelId(null);
            setChannelMessages([]);
            break;

          case "GUILD_JOINED":
            setGuilds((prev) =>
              prev.some((g) => g.guild_id === msg.guild_id)
                ? prev
                : [...prev, { guild_id: msg.guild_id, name: msg.name, owner_id: msg.owner_id }]
            );
            setMyGuildIds((prev) => new Set(prev).add(msg.guild_id));
            setActiveGuildId(msg.guild_id);
            setChannels(msg.channels ?? []);
            setActiveChannelId(null);
            setChannelMessages([]);
            break;

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
              }
            }
            break;

          case "GUILD_DELETED":
            setGuilds((prev) => prev.filter((g) => g.guild_id !== msg.guild_id));
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
            }
            break;

          case "CHANNEL_LIST":
            setChannels(msg.channels ?? []);
            break;

          case "CHANNEL_CREATED":
            if (msg.guild_id === activeGuildIdRef.current) {
              setChannels((prev) => [
                ...prev,
                { channel_id: msg.channel_id, name: msg.name, channel_type: msg.channel_type }
              ]);
            }
            break;

          case "CHANNEL_DELETED":
            if (msg.guild_id === activeGuildIdRef.current) {
              setChannels((prev) => prev.filter((c) => c.channel_id !== msg.channel_id));
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
            setChannelMessages((prev) => [...prev, msg]);
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

  const handleSend = () => {
    if (input.trim()) {
      sendChatMessage(input);
      setInput("");
    }
  };

  const handleKeyDown = (e: React.KeyboardEvent<HTMLInputElement>) => {
    if (e.key === "Enter") {
      handleSend();
    }
  };

  const handleCreateGuild = () => {
    if (guildNameInput.trim()) {
      createGuild(guildNameInput.trim());
      setGuildNameInput("");
    }
  };

  const handleSelectGuild = (guildId: string) => {
    setActiveGuildId(guildId);
    listChannels(guildId);
  };

  const handleJoinGuild = (guildId: string) => {
    joinGuild(guildId);
  };

  const handleCreateChannel = () => {
    if (activeGuildId && channelNameInput.trim()) {
      createChannel(activeGuildId, channelNameInput.trim(), "TEXT");
      setChannelNameInput("");
    }
  };

  const handleJoinChannel = (channelId: string) => {
    joinChannel(channelId);
  };

  const handleSendChannelMessage = () => {
    if (channelMessageInput.trim()) {
      sendChannelMessage(channelMessageInput);
      setChannelMessageInput("");
    }
  };

  const activeGuild = guilds.find((g) => g.guild_id === activeGuildId) ?? null;
  const isOwner = !!activeGuild && !!myUserId && activeGuild.owner_id === myUserId;

  return (
    <main style={{ padding: "2rem", fontFamily: "monospace" }}>
      <h1>CIG Nexus Web Client</h1>

      <div style={{ marginTop: "1rem" }}>
        <strong>Status:</strong>{" "}
        <span
          style={{
            color: status === "connected" ? "green" : status === "error" ? "red" : "gray"
          }}
        >
          {status}
        </span>
      </div>

      {lastError && (
        <div className="mt-4 flex items-center justify-between rounded bg-red-100 px-3 py-2 text-red-800">
          <span>{lastError}</span>
          <button onClick={() => setLastError(null)} className="ml-4 font-semibold">
            &times;
          </button>
        </div>
      )}

      <div style={{ marginTop: "2rem", display: "flex", gap: "0.5rem" }}>
        <button
          onClick={() => setView("chat")}
          style={{
            padding: "0.5rem 1rem",
            borderRadius: "4px",
            border: "1px solid #ccc",
            background: view === "chat" ? "#007bff" : "#eee",
            color: view === "chat" ? "white" : "black",
            cursor: "pointer",
            fontFamily: "monospace"
          }}
        >
          Global Chat
        </button>
        <button
          onClick={() => setView("guilds")}
          style={{
            padding: "0.5rem 1rem",
            borderRadius: "4px",
            border: "1px solid #ccc",
            background: view === "guilds" ? "#007bff" : "#eee",
            color: view === "guilds" ? "white" : "black",
            cursor: "pointer",
            fontFamily: "monospace"
          }}
        >
          Guilds
        </button>
      </div>

      {view === "chat" && (
        <>
          <div style={{ marginTop: "2rem" }}>
            <h2>Chat</h2>
            <div style={{ display: "flex", gap: "0.5rem", marginBottom: "1rem" }}>
              <input
                type="text"
                value={input}
                onChange={(e) => setInput(e.target.value)}
                onKeyDown={handleKeyDown}
                placeholder="Type a message..."
                style={{
                  flex: 1,
                  padding: "0.5rem",
                  borderRadius: "4px",
                  border: "1px solid #ccc",
                  fontFamily: "monospace"
                }}
              />
              <button
                onClick={handleSend}
                disabled={status !== "connected"}
                style={{
                  padding: "0.5rem 1rem",
                  borderRadius: "4px",
                  border: "1px solid #ccc",
                  background: status === "connected" ? "#007bff" : "#ccc",
                  color: "white",
                  cursor: status === "connected" ? "pointer" : "not-allowed",
                  fontFamily: "monospace"
                }}
              >
                Send
              </button>
            </div>
          </div>

          <div style={{ marginTop: "2rem" }}>
            <h2>Messages</h2>
            {chatMessages.length === 0 ? (
              <p>No messages received yet...</p>
            ) : (
              <ul style={{ listStyle: "none", padding: 0 }}>
                {chatMessages.map((msg, index) => (
                  <li key={index} className="mb-2 rounded bg-gray-100 px-3 py-2 text-black">
                    <div className="flex items-baseline gap-2">
                      <span className="font-semibold">
                        {msg.username ?? msg.from ?? "anonymous"}
                      </span>
                      <span className="text-xs text-gray-500">
                        {new Date(msg.timestamp * 1000).toLocaleTimeString()}
                      </span>
                    </div>
                    <div>{msg.content}</div>
                  </li>
                ))}
              </ul>
            )}
          </div>
        </>
      )}

      {view === "guilds" && (
        <div style={{ marginTop: "2rem", display: "flex", gap: "2rem" }}>
          <div style={{ minWidth: "200px" }}>
            <h2>Guilds</h2>
            <ul style={{ listStyle: "none", padding: 0 }}>
              {guilds.map((g) => {
                const isMember = myGuildIds.has(g.guild_id);
                return (
                  <li
                    key={g.guild_id}
                    className="mb-2 flex items-center justify-between gap-2 rounded bg-gray-100 px-3 py-2 text-black"
                  >
                    {isMember ? (
                      <button
                        onClick={() => handleSelectGuild(g.guild_id)}
                        className={g.guild_id === activeGuildId ? "font-semibold" : ""}
                      >
                        {g.name}
                      </button>
                    ) : (
                      <>
                        <span>{g.name}</span>
                        <button
                          onClick={() => handleJoinGuild(g.guild_id)}
                          className="rounded bg-blue-600 px-2 py-1 text-xs text-white"
                        >
                          Join
                        </button>
                      </>
                    )}
                  </li>
                );
              })}
            </ul>

            <div style={{ display: "flex", gap: "0.5rem", marginTop: "1rem" }}>
              <input
                type="text"
                value={guildNameInput}
                onChange={(e) => setGuildNameInput(e.target.value)}
                placeholder="New guild name..."
                style={{
                  flex: 1,
                  padding: "0.5rem",
                  borderRadius: "4px",
                  border: "1px solid #ccc",
                  fontFamily: "monospace"
                }}
              />
              <button
                onClick={handleCreateGuild}
                style={{
                  padding: "0.5rem 1rem",
                  borderRadius: "4px",
                  border: "1px solid #ccc",
                  background: "#007bff",
                  color: "white",
                  cursor: "pointer",
                  fontFamily: "monospace"
                }}
              >
                Create Guild
              </button>
            </div>
          </div>

          <div style={{ flex: 1 }}>
            {!activeGuild ? (
              <p>Select or create a guild to get started.</p>
            ) : (
              <>
                <h2>{activeGuild.name}</h2>

                <div style={{ display: "flex", flexWrap: "wrap", gap: "0.5rem", marginTop: "0.5rem" }}>
                  {channels.map((c) => (
                    <button
                      key={c.channel_id}
                      onClick={() => handleJoinChannel(c.channel_id)}
                      className={
                        c.channel_id === activeChannelId
                          ? "rounded bg-blue-600 px-2 py-1 text-xs text-white"
                          : "rounded bg-gray-200 px-2 py-1 text-xs text-black"
                      }
                    >
                      #{c.name}
                    </button>
                  ))}
                </div>

                {isOwner && (
                  <div style={{ display: "flex", gap: "0.5rem", marginTop: "0.5rem" }}>
                    <input
                      type="text"
                      value={channelNameInput}
                      onChange={(e) => setChannelNameInput(e.target.value)}
                      placeholder="New channel name..."
                      style={{
                        flex: 1,
                        padding: "0.5rem",
                        borderRadius: "4px",
                        border: "1px solid #ccc",
                        fontFamily: "monospace"
                      }}
                    />
                    <button
                      onClick={handleCreateChannel}
                      style={{
                        padding: "0.5rem 1rem",
                        borderRadius: "4px",
                        border: "1px solid #ccc",
                        background: "#007bff",
                        color: "white",
                        cursor: "pointer",
                        fontFamily: "monospace"
                      }}
                    >
                      Create Channel
                    </button>
                  </div>
                )}

                <div style={{ marginTop: "1.5rem" }}>
                  {!activeChannelId ? (
                    <p>Select a channel to start chatting.</p>
                  ) : (
                    <>
                      <div style={{ display: "flex", gap: "0.5rem", marginBottom: "1rem" }}>
                        <input
                          type="text"
                          value={channelMessageInput}
                          onChange={(e) => setChannelMessageInput(e.target.value)}
                          onKeyDown={(e) => {
                            if (e.key === "Enter") {
                              handleSendChannelMessage();
                            }
                          }}
                          placeholder="Type a message..."
                          style={{
                            flex: 1,
                            padding: "0.5rem",
                            borderRadius: "4px",
                            border: "1px solid #ccc",
                            fontFamily: "monospace"
                          }}
                        />
                        <button
                          onClick={handleSendChannelMessage}
                          style={{
                            padding: "0.5rem 1rem",
                            borderRadius: "4px",
                            border: "1px solid #ccc",
                            background: "#007bff",
                            color: "white",
                            cursor: "pointer",
                            fontFamily: "monospace"
                          }}
                        >
                          Send
                        </button>
                      </div>

                      {channelMessages.length === 0 ? (
                        <p>No messages in this channel yet...</p>
                      ) : (
                        <ul style={{ listStyle: "none", padding: 0 }}>
                          {channelMessages.map((msg, index) => (
                            <li
                              key={index}
                              className="mb-2 rounded bg-gray-100 px-3 py-2 text-black"
                            >
                              <div className="flex items-baseline gap-2">
                                <span className="font-semibold">{msg.username}</span>
                                <span className="text-xs text-gray-500">
                                  {new Date(msg.timestamp * 1000).toLocaleTimeString()}
                                </span>
                              </div>
                              <div>{msg.content}</div>
                            </li>
                          ))}
                        </ul>
                      )}
                    </>
                  )}
                </div>
              </>
            )}
          </div>
        </div>
      )}
    </main>
  );
}
