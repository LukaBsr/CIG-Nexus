import net from "node:net";

import { encodeFrame, extractFrames } from "../protocol/frame";

export class TcpClient {
	private readonly host: string;
	private readonly port: number;
	private socket: net.Socket | null = null;
	private buffer: Buffer = Buffer.alloc(0);
	private messageCallback: ((payload: Buffer) => void) | null = null;
	private closeCallback: (() => void) | null = null;

	constructor(host: string, port: number) {
		this.host = host;
		this.port = port;
	}

	connect(): Promise<void> {
		return new Promise((resolve, reject) => {
			const socket = new net.Socket();
			this.socket = socket;

			socket.once("connect", () => {
				resolve();
			});

			socket.once("error", (error) => {
				socket.destroy();
				reject(error);
			});

			socket.on("data", (chunk) => {
				this.buffer = Buffer.concat([this.buffer, chunk]);
				const { frames, remaining } = extractFrames(this.buffer);
				this.buffer = remaining;

				if (this.messageCallback) {
					for (const frame of frames) {
						this.messageCallback(frame);
					}
				}
			});

			socket.on("close", () => {
				if (this.closeCallback) {
					this.closeCallback();
				}
			});

			socket.on("error", () => {
				socket.destroy();
			});

			socket.connect(this.port, this.host);
		});
	}

	send(payload: Buffer): void {
		if (!this.socket || this.socket.destroyed) {
			return;
		}

		const frame = encodeFrame(payload);
		this.socket.write(frame);
	}

	onMessage(callback: (payload: Buffer) => void): void {
		this.messageCallback = callback;
	}

	onClose(callback: () => void): void {
		this.closeCallback = callback;
	}

	close(): void {
		if (!this.socket) {
			return;
		}

		this.socket.end();
		this.socket.destroy();
	}
}
