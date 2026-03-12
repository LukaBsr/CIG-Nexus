# CIG Nexus Desktop Client

The desktop client is not the primary active client path at the current stage of the project.

## Current Status

- the `desktop/` area exists in the repository
- no documented end-to-end desktop flow is maintained yet
- the actively implemented client path is the Next.js web client through the WebSocket gateway

## Intended Direction

The desktop client is expected to act as a native client for CIG Nexus in a later iteration.

Likely goals:

- direct connectivity to the CIG Nexus server
- reuse of the shared JSON protocol
- richer desktop-specific UI and packaging

## For Now

If you want to run the currently supported application flow, use:

- the web client in `web/`
- the gateway in `gateway/`
- the server in `server/`

See the root [README.md](../README.md) for the current development and Docker workflow.
