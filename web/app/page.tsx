"use client";

import { useEffect, useRef, useState } from "react";
import {
  connect,
  sendChatMessage,
  createGuild,
  listGuilds,
  joinGuild,
  leaveGuild,
  deleteGuild,
  listChannels,
  createChannel,
  deleteChannel,
  joinChannel,
  leaveChannel,
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
                  <span className="font-semibold">{msg.username ?? msg.from ?? "anonymous"}</span>
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
    </main>
  );
}
