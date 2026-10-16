import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";

import { createMarkReadScheduler, MARK_READ_DEBOUNCE_MS, MARK_READ_RETRY_MS } from "./markReadScheduler";

describe("markReadScheduler", () => {
  let send: ReturnType<typeof vi.fn<(peerId: string, upTo: number) => void>>;

  beforeEach(() => {
    vi.useFakeTimers();
    send = vi.fn<(peerId: string, upTo: number) => void>();
  });

  afterEach(() => {
    vi.useRealTimers();
  });

  it("debounces a burst into one send for the highest message_id", () => {
    const scheduler = createMarkReadScheduler(send);
    scheduler.request("u_2", 5);
    vi.advanceTimersByTime(MARK_READ_DEBOUNCE_MS - 1);
    scheduler.request("u_2", 7);
    scheduler.request("u_2", 6);
    vi.advanceTimersByTime(MARK_READ_DEBOUNCE_MS - 1);
    expect(send).not.toHaveBeenCalled();

    vi.advanceTimersByTime(1);
    expect(send).toHaveBeenCalledTimes(1);
    expect(send).toHaveBeenCalledWith("u_2", 7);
  });

  it("keeps a single request in flight, sending the next only once it settles", () => {
    const scheduler = createMarkReadScheduler(send);
    scheduler.request("u_2", 5);
    vi.advanceTimersByTime(MARK_READ_DEBOUNCE_MS);
    expect(send).toHaveBeenCalledTimes(1);

    scheduler.request("u_2", 8);
    vi.advanceTimersByTime(MARK_READ_DEBOUNCE_MS * 3);
    expect(send).toHaveBeenCalledTimes(1);

    scheduler.onDmRead("u_2", 5);
    expect(send).toHaveBeenCalledTimes(2);
    expect(send).toHaveBeenLastCalledWith("u_2", 8);
  });

  it("skips a request already covered by a DM_READ marker", () => {
    const scheduler = createMarkReadScheduler(send);
    scheduler.onDmRead("u_2", 9);
    scheduler.request("u_2", 9);
    vi.advanceTimersByTime(MARK_READ_DEBOUNCE_MS);
    expect(send).not.toHaveBeenCalled();
  });

  it("retries once after a failure", () => {
    const scheduler = createMarkReadScheduler(send);
    scheduler.request("u_2", 5);
    vi.advanceTimersByTime(MARK_READ_DEBOUNCE_MS);
    scheduler.onError();

    vi.advanceTimersByTime(MARK_READ_RETRY_MS - 1);
    expect(send).toHaveBeenCalledTimes(1);
    vi.advanceTimersByTime(1);
    expect(send).toHaveBeenCalledTimes(2);
    expect(send).toHaveBeenLastCalledWith("u_2", 5);

    scheduler.onDmRead("u_2", 5);
    scheduler.request("u_2", 6);
    vi.advanceTimersByTime(MARK_READ_DEBOUNCE_MS);
    expect(send).toHaveBeenCalledTimes(3);
  });

  it("gives up after the retry fails too, until the next trigger", () => {
    const scheduler = createMarkReadScheduler(send);
    scheduler.request("u_2", 5);
    vi.advanceTimersByTime(MARK_READ_DEBOUNCE_MS);
    scheduler.onError();
    vi.advanceTimersByTime(MARK_READ_RETRY_MS);
    expect(send).toHaveBeenCalledTimes(2);

    scheduler.onError();
    vi.advanceTimersByTime(MARK_READ_RETRY_MS * 10);
    expect(send).toHaveBeenCalledTimes(2);

    // The next trigger starts fresh rather than being blocked by the
    // abandoned request.
    scheduler.request("u_2", 5);
    vi.advanceTimersByTime(MARK_READ_DEBOUNCE_MS);
    expect(send).toHaveBeenCalledTimes(3);
  });

  it("ignores an ERROR when nothing is in flight", () => {
    const scheduler = createMarkReadScheduler(send);
    scheduler.onError();
    vi.advanceTimersByTime(MARK_READ_RETRY_MS * 2);
    expect(send).not.toHaveBeenCalled();
  });
});
