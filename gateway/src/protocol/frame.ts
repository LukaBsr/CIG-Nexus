export const MAX_FRAME_SIZE = 1024 * 1024;

export function encodeFrame(payload: Buffer): Buffer {
  if (payload.length === 0) {
    throw new Error("Payload must not be empty.");
  }

  if (payload.length > MAX_FRAME_SIZE) {
    throw new Error("Payload exceeds maximum frame size.");
  }

  const frame = Buffer.allocUnsafe(4 + payload.length);
  frame.writeUInt32BE(payload.length, 0);
  payload.copy(frame, 4);
  return frame;
}

export function extractFrames(buffer: Buffer): { frames: Buffer[]; remaining: Buffer } {
  const frames: Buffer[] = [];
  let offset = 0;

  while (offset + 4 <= buffer.length) {
    const size = buffer.readUInt32BE(offset);

    if (size === 0 || size > MAX_FRAME_SIZE) {
      throw new Error("Invalid frame size.");
    }

    const frameEnd = offset + 4 + size;
    if (frameEnd > buffer.length) {
      break;
    }

    frames.push(buffer.slice(offset + 4, frameEnd));
    offset = frameEnd;
  }

  return {
    frames,
    remaining: buffer.slice(offset)
  };
}
