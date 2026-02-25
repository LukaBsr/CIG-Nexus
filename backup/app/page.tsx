"use client";

import { useEffect, useState } from "react";
import { connect } from "../lib/gateway";

export default function Home() {
  const [status, setStatus] = useState<string>("disconnected");
  const [messages, setMessages] = useState<any[]>([]);

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
        <h2>Messages</h2>
        {messages.length === 0 ? (
          <p>No messages received yet...</p>
        ) : (
          <ul style={{ listStyle: "none", padding: 0 }}>
            {messages.map((msg, index) => (
              <li
                key={index}
                style={{
                  background: "#f5f5f5",
                  padding: "0.5rem",
                  marginBottom: "0.5rem",
                  borderRadius: "4px"
                }}
              >
                <pre style={{ margin: 0 }}>{JSON.stringify(msg, null, 2)}</pre>
              </li>
            ))}
          </ul>
        )}
      </div>
    </main>
  );
}
