"use client";

import { useEffect, useState } from "react";
import { connect, sendChatMessage } from "../lib/gateway";

export default function Home() {
  const [status, setStatus] = useState<string>("disconnected");
  const [messages, setMessages] = useState<any[]>([]);
  const [input, setInput] = useState<string>("");

  useEffect(() => {
    connect(
      (msg) => {
        setMessages((prev) => [...prev, msg]);
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
        {messages.length === 0 ? (
          <p>No messages received yet...</p>
        ) : (
          <ul style={{ listStyle: "none", padding: 0 }}>
            {messages.map((msg, index) =>
              msg.type === "CHAT_MESSAGE" ? (
                <li key={index} className="mb-2 rounded bg-gray-100 px-3 py-2 text-black">
                  <div className="flex items-baseline gap-2">
                    <span className="font-semibold">{msg.username ?? msg.from ?? "anonymous"}</span>
                    <span className="text-xs text-gray-500">
                      {new Date(msg.timestamp * 1000).toLocaleTimeString()}
                    </span>
                  </div>
                  <div>{msg.content}</div>
                </li>
              ) : (
                <li key={index} className="mb-2 rounded bg-gray-100 px-3 py-2 text-xs text-gray-500">
                  {msg.type}
                </li>
              )
            )}
          </ul>
        )}
      </div>
    </main>
  );
}
