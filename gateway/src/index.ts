import { WsServer } from "./ws/WsServer";

const wsPort = Number.parseInt(process.env.WS_PORT ?? "8080", 10);
const tcpHost = process.env.TCP_HOST ?? "localhost";
const tcpPort = Number.parseInt(process.env.TCP_PORT ?? "4242", 10);

const server = new WsServer({ wsPort, tcpHost, tcpPort });

console.log("Starting WebSocket gateway...");
console.log(`WS_PORT=${wsPort} TCP_HOST=${tcpHost} TCP_PORT=${tcpPort}`);

server.start();
