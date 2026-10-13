# CIG Nexus Web Client

The web app is a Next.js (App Router) application. It serves the chat UI, hosts the Discord OAuth2 login and session-token issuance, and owns all Postgres/Redis access — the C++ server reaches durable state only through this app's internal-only API.

## Current Features

- Discord OAuth2 (PKCE) login, a public landing page, and a `/login-error` page
- Browser WebSocket connection to the gateway with automatic `HELLO`, then `IDENTIFY` with a signed session token fetched from `GET /api/auth/session-token` (no client-chosen username)
- Live connection status display
- Global lobby chat
- A persistent, Discord-style guild rail: Lobby, one icon per joined guild, and Friends, with guild browse/join/create behind a `+` popover
- Guilds: open/application/private visibility, invites (with optional max uses and expiry), join requests and their approval inbox, officer-gated channel creation, per-channel chat with history, and a member roster with rank-based roles and live presence
- Friends: friend requests (direct or by shareable code), blocking, and 1:1 direct messages with history
- Settings modal: Profile (display name, avatar, bio, status message, accent color), Appearance (8 themes — `abyss`, `ember`, `onyx`, `mocha`, `amethyst`, `espresso`, `daylight`, `cloud` — optionally synced to the account), and Blocked Users

## How It Works

The browser cannot connect directly to the TCP server, so it talks to the gateway instead.

1. The user logs in with Discord; Next.js sets an httpOnly session cookie.
2. The page opens a WebSocket to the gateway (`NEXT_PUBLIC_GATEWAY_URL`) and sends:

```json
{
	"type": "HELLO",
	"version": "0.1",
	"client": "web"
}
```

3. After `WELCOME`, the client fetches a short-lived session token and sends:

```json
{
	"type": "IDENTIFY",
	"session_token": "<JWT>"
}
```

4. Incoming server messages are parsed as JSON in `hooks/useGatewayConnection.ts` and routed by `type` into UI state. Wire messages stay snake_case; this hook is the only place they are mapped to camelCase (see [../docs/frontend-rebuild-plan.md](../docs/frontend-rebuild-plan.md#wire-message-casing-convention)).

See [../shared/protocol/README.md](../shared/protocol/README.md) for the full message set.

## Local Development

Install dependencies:

```bash
npm install
```

Run the app:

```bash
npm run dev
```

Open `http://localhost:3000` in the browser. Login and the internal API need Postgres, Redis, and the auth environment variables — the simplest way to run everything is `docker compose up --build` from the repository root (see the root README).

Other scripts: `npm run lint`, `npm run build`, and `npm test` (Vitest; needs a running Docker daemon, since the suite starts Postgres and Redis containers with testcontainers and builds the web image).

## Environment

Required auth/persistence variables (the process fails fast at startup if any are missing) are documented in [../.env.example](../.env.example).

The gateway URL comes from:

- `NEXT_PUBLIC_GATEWAY_URL`

Default fallback:

- `ws://localhost:8080`

## Current Structure

```text
app/
├── page.tsx        # the chat UI (lobby, guilds, friends)
├── api/            # browser-facing routes: OAuth, session token, user profile/appearance
├── internal/       # internal-only API called by the C++ server (unreachable on the public port)
└── uploads/        # dynamic route serving uploaded avatars

components/         # extracted UI components (MessageList, GuildRail, FriendsView, SettingsModal, ...)
hooks/              # useGatewayConnection: the WebSocket connection + protocol switch
lib/                # gateway client, wire types, auth, internal-API logic, settings, appearance
db/                 # Drizzle schema and migrations
server.mjs          # production server: public port + a second, unpublished internal port
```

## UI Notes

- Tailwind CSS only; theme colors are CSS custom properties switched by a `data-theme` attribute
- No external UI or state-management library
- Server-authoritative: the UI reflects what the server confirms rather than inventing optimistic state

## Current Limitations

- No reconnection logic
- No leave/delete controls in the UI for guilds or channels (the `lib/gateway.ts` helpers exist, just aren't wired into the UI yet)
- Profile bio, status message, and accent color are editable but not displayed anywhere
- No custom guild icons (a deterministic initials icon is used)
- Voice channels are metadata only

## Docker

The web client is included in the root Docker Compose stack.

From the repository root, after creating `.env` and `secrets/` as described in the root [README](../README.md) (`docker compose` refuses to run with required variables missing from `.env`):

```bash
docker compose up --build
```

The app will be available at `http://localhost:3000`.

## Related Documentation

- See [../gateway/README.md](../gateway/README.md) for gateway details.
- See [../shared/protocol/README.md](../shared/protocol/README.md) for protocol payloads.
- See [../docs/auth/discord-design.md](../docs/auth/discord-design.md) for the OAuth2 and internal-API design.
