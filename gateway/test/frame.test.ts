import assert from "node:assert/strict";
import { describe, it } from "node:test";

import { encodeFrame, extractFrames, MAX_FRAME_SIZE } from "../src/protocol/frame";

describe("encodeFrame", () => {
  it("prefixes the payload with its length as a 4-byte big-endian integer", () => {
    const frame = encodeFrame(Buffer.from("hello"));

    assert.equal(frame.length, 9);
    assert.equal(frame.readUInt32BE(0), 5);
    assert.equal(frame.subarray(4).toString(), "hello");
  });

  it("rejects an empty payload", () => {
    assert.throws(() => encodeFrame(Buffer.alloc(0)), /must not be empty/);
  });

  it("rejects a payload over the 1 MiB maximum, and accepts exactly the maximum", () => {
    assert.throws(() => encodeFrame(Buffer.alloc(MAX_FRAME_SIZE + 1)), /maximum frame size/);
    assert.equal(encodeFrame(Buffer.alloc(MAX_FRAME_SIZE)).length, MAX_FRAME_SIZE + 4);
  });
});

describe("extractFrames", () => {
  it("round-trips a frame produced by encodeFrame", () => {
    const { frames, remaining } = extractFrames(encodeFrame(Buffer.from('{"type":"HELLO"}')));

    assert.deepEqual(
      frames.map((f) => f.toString()),
      ['{"type":"HELLO"}']
    );
    assert.equal(remaining.length, 0);
  });

  it("extracts several frames delivered in one chunk", () => {
    const chunk = Buffer.concat([encodeFrame(Buffer.from("a")), encodeFrame(Buffer.from("bb"))]);

    const { frames, remaining } = extractFrames(chunk);

    assert.deepEqual(
      frames.map((f) => f.toString()),
      ["a", "bb"]
    );
    assert.equal(remaining.length, 0);
  });

  it("keeps a partial frame buffered until the rest arrives", () => {
    const whole = encodeFrame(Buffer.from("partial"));

    const first = extractFrames(whole.subarray(0, 6));
    assert.equal(first.frames.length, 0);
    assert.equal(first.remaining.length, 6);

    const second = extractFrames(Buffer.concat([first.remaining, whole.subarray(6)]));
    assert.deepEqual(
      second.frames.map((f) => f.toString()),
      ["partial"]
    );
  });

  it("keeps a header-only fragment (fewer than 4 bytes) buffered", () => {
    const { frames, remaining } = extractFrames(Buffer.from([0, 0]));

    assert.equal(frames.length, 0);
    assert.equal(remaining.length, 2);
  });

  it("throws on a zero-length or oversized frame header", () => {
    const zero = Buffer.alloc(4);
    const oversized = Buffer.alloc(4);
    oversized.writeUInt32BE(MAX_FRAME_SIZE + 1, 0);

    assert.throws(() => extractFrames(zero), /Invalid frame size/);
    assert.throws(() => extractFrames(oversized), /Invalid frame size/);
  });
});
