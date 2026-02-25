let ws: WebSocket | null = null;

export function connect(
  onMessage: (msg: any) => void,
  onStatus: (status: string) => void
): void {
  ws = new WebSocket("ws://localhost:8080");

  ws.onopen = () => {
    onStatus("connected");
    ws?.send(
      JSON.stringify({
        type: "HELLO",
        version: "0.1",
        client: "web"
      })
    );
  };

  ws.onmessage = (event) => {
    try {
      const message = JSON.parse(event.data);
      onMessage(message);
    } catch (error) {
      console.error("Failed to parse message:", error);
    }
  };

  ws.onclose = () => {
    onStatus("disconnected");
  };

  ws.onerror = () => {
    onStatus("error");
  };
}
