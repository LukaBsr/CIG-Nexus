import WebSocket, { WebSocketServer } from "ws";

import { TcpClient } from "../tcp/TcpClient";

type WsServerOptions = {
	wsPort: number;
	tcpHost: string;
	tcpPort: number;
};

export class WsServer {
	private readonly options: WsServerOptions;

	constructor(options: { wsPort: number; tcpHost: string; tcpPort: number }) {
		this.options = options;
	}

	start(): void {
		const server = new WebSocketServer({ port: this.options.wsPort });
		console.log(`WebSocket gateway listening on port ${this.options.wsPort}`);
        
		server.on("connection", (ws) => {
			const tcp = new TcpClient(this.options.tcpHost, this.options.tcpPort);
			let closed = false;

			const closeBoth = () => {
				if (closed) {
					return;
				}

				closed = true;
				tcp.close();
				ws.close();
			};

			tcp.onMessage((payload) => {
				if (ws.readyState !== WebSocket.OPEN) {
					return;
				}

				ws.send(payload.toString());
			});

			tcp.onClose(() => {
				closeBoth();
			});

			ws.on("message", (data) => {
				const message =
					typeof data === "string" ? data : Buffer.from(data as Buffer).toString();
				tcp.send(Buffer.from(message));
			});

			ws.on("close", () => {
				closeBoth();
			});

			ws.on("error", () => {
				closeBoth();
			});

			tcp
				.connect()
				.catch(() => {
					if (ws.readyState === WebSocket.OPEN) {
						ws.send(
							JSON.stringify({
								type: "GATEWAY_ERROR",
								message: "Unable to connect to server"
							})
						);
					}

					closeBoth();
				});
		});
	}
}
