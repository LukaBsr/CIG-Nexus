import assert from "node:assert/strict";
import net from "node:net";
import { after, before, describe, it } from "node:test";

import WebSocket, { WebSocketServer } from "ws";

import { encodeFrame, extractFrames } from "../src/protocol/frame";
import { WsServer } from "../src/ws/WsServer";

// A stand-in for the C++ server: speaks the 4-byte-framed TCP protocol,
// records every payload it receives, and can push a payload back.
class FakeTcpServer {
  readonly received: string[] = [];
  readonly sockets: net.Socket[] = [];
  private readonly server: net.Server;

  constructor() {
    this.server = net.createServer((socket) => {
      this.sockets.push(socket);
      let buffer: Buffer = Buffer.alloc(0);
      socket.on("data", (chunk) => {
        buffer = Buffer.concat([buffer, chunk]);
        const { frames, remaining } = extractFrames(buffer);
        buffer = remaining;
        for (const frame of frames) {
          this.received.push(frame.toString());
        }
      });
      socket.on("error", () => {});
    });
  }

  listen(): Promise<number> {
    return new Promise((resolve) => {
      this.server.listen(0, "127.0.0.1", () => resolve((this.server.address() as net.AddressInfo).port));
    });
  }

  close(): Promise<void> {
    for (const s of this.sockets) {
      s.destroy();
    }
    return new Promise((resolve) => this.server.close(() => resolve()));
  }
}

function nextMessage(ws: WebSocket): Promise<string> {
  return new Promise((resolve, reject) => {
    ws.once("message", (data) => resolve(data.toString()));
    ws.once("error", reject);
  });
}

function opened(ws: WebSocket): Promise<void> {
  return new Promise((resolve, reject) => {
    ws.once("open", () => resolve());
    ws.once("error", reject);
  });
}

function closed(ws: WebSocket): Promise<void> {
  return new Promise((resolve) => {
    if (ws.readyState === WebSocket.CLOSED) {
      resolve();
      return;
    }
    ws.once("close", () => resolve());
  });
}

async function waitFor(condition: () => boolean, timeoutMs = 2000): Promise<void> {
  const start = Date.now();
  while (!condition()) {
    if (Date.now() - start > timeoutMs) {
      throw new Error("timed out waiting for condition");
    }
    await new Promise((r) => setTimeout(r, 10));
  }
}

describe("gateway WebSocket <-> TCP bridge", () => {
  let tcp: FakeTcpServer;
  let tcpPort: number;
  let gateway: WebSocketServer;
  let wsPort: number;

  before(async () => {
    tcp = new FakeTcpServer();
    tcpPort = await tcp.listen();
    gateway = new WsServer({ wsPort: 0, tcpHost: "127.0.0.1", tcpPort }).start();
    await new Promise<void>((resolve) => gateway.once("listening", () => resolve()));
    wsPort = (gateway.address() as net.AddressInfo).port;
  });

  after(async () => {
    for (const client of gateway.clients) {
      client.terminate();
    }
    await new Promise<void>((resolve) => gateway.close(() => resolve()));
    await tcp.close();
  });

  it("frames a WebSocket text message onto the TCP connection unchanged", async () => {
    tcp.received.length = 0;
    const ws = new WebSocket(`ws://127.0.0.1:${wsPort}`);
    await opened(ws);

    const hello = '{"type":"HELLO","version":"0.1","client":"web"}';
    ws.send(hello);

    await waitFor(() => tcp.received.length === 1);
    assert.equal(tcp.received[0], hello);
    ws.close();
    await closed(ws);
  });

  it("forwards a framed TCP payload back to the browser as text", async () => {
    const socketsBefore = tcp.sockets.length;
    const ws = new WebSocket(`ws://127.0.0.1:${wsPort}`);
    await opened(ws);
    await waitFor(() => tcp.sockets.length === socketsBefore + 1);

    const reply = nextMessage(ws);
    tcp.sockets[tcp.sockets.length - 1].write(encodeFrame(Buffer.from('{"type":"WELCOME"}')));

    assert.equal(await reply, '{"type":"WELCOME"}');
    ws.close();
    await closed(ws);
  });

  it("closes the TCP side when the browser disconnects", async () => {
    const socketsBefore = tcp.sockets.length;
    const ws = new WebSocket(`ws://127.0.0.1:${wsPort}`);
    await opened(ws);
    await waitFor(() => tcp.sockets.length === socketsBefore + 1);
    const tcpSide = tcp.sockets[tcp.sockets.length - 1];
    const tcpClosed = new Promise<void>((resolve) => tcpSide.once("close", () => resolve()));

    ws.close();

    await tcpClosed;
  });

  it("closes the browser side when the TCP server disconnects", async () => {
    const socketsBefore = tcp.sockets.length;
    const ws = new WebSocket(`ws://127.0.0.1:${wsPort}`);
    await opened(ws);
    await waitFor(() => tcp.sockets.length === socketsBefore + 1);

    tcp.sockets[tcp.sockets.length - 1].destroy();

    await closed(ws);
  });
});

describe("gateway when the TCP server is unreachable", () => {
  it("sends GATEWAY_ERROR and closes the WebSocket", async () => {
    // Bind then release a port so nothing is listening on it.
    const probe = net.createServer();
    await new Promise<void>((resolve) => probe.listen(0, "127.0.0.1", () => resolve()));
    const deadPort = (probe.address() as net.AddressInfo).port;
    await new Promise<void>((resolve) => probe.close(() => resolve()));

    const gateway = new WsServer({ wsPort: 0, tcpHost: "127.0.0.1", tcpPort: deadPort }).start();
    await new Promise<void>((resolve) => gateway.once("listening", () => resolve()));
    const port = (gateway.address() as net.AddressInfo).port;

    const ws = new WebSocket(`ws://127.0.0.1:${port}`);
    const message = nextMessage(ws);
    const wsClosed = closed(ws);

    assert.deepEqual(JSON.parse(await message), {
      type: "GATEWAY_ERROR",
      message: "Unable to connect to server"
    });
    await wsClosed;

    await new Promise<void>((resolve) => gateway.close(() => resolve()));
  });
});
