# CIG Nexus Web Client

The web client is a Next.js App Router application that connects to the CIG Nexus gateway through a browser WebSocket connection.

## Current Features

- client-side WebSocket connection to the gateway
- automatic `HELLO` message on connect
- automatic `IDENTIFY` after `WELCOME` (default username: `web_user`)
- live connection status display
- simple chat input with send button
- live list of received messages rendered as formatted JSON

## How It Works

The browser cannot connect directly to the TCP server, so it talks to the gateway instead.

Flow:

1. The page mounts.
2. The client opens a WebSocket connection to the gateway.
3. On open, the client automatically sends:

```json
{
	"type": "HELLO",
	"version": "0.1",
	"client": "web"
}
```

4. The user can then send chat messages such as:

```json
{
	"type": "CHAT_MESSAGE",
	"content": "hello"
}
```

Before chat, the client also sends:

```json
{
	"type": "IDENTIFY",
	"username": "web_user"
}
```

5. Incoming server responses are parsed as JSON and appended to the on-screen message list.

## Local Development

Install dependencies:

```bash
npm install
```

Run the app:

```bash
npm run dev
```

Open `http://localhost:3000` in the browser.

## Environment

The web client reads the gateway URL from:

- `NEXT_PUBLIC_GATEWAY_URL`

Default fallback:

- `ws://localhost:8080`

## Current Structure

```text
app/
└── page.tsx        # main chat UI

lib/
└── gateway.ts      # browser WebSocket client helpers
```

## UI Notes

The current UI is intentionally minimal:

- single page
- simple inline styles
- no external UI library
- no state management library
- no routing beyond the default page

## Current Limitations

- no authentication
- no custom username selection yet (hardcoded `web_user`)
- no message history persistence
- no reconnection logic
- no optimistic UI state
- no dedicated chat message styling beyond raw JSON rendering

## Docker

The web client is included in the root Docker Compose stack.

From the repository root:

```bash
docker compose up --build
```

The app will be available at `http://localhost:3000`.

## Related Documentation

- See [../gateway/README.md](../gateway/README.md) for gateway details.
- See [../shared/protocol/README.md](../shared/protocol/README.md) for protocol payloads.
