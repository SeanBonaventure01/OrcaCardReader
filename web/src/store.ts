import { signal } from '@preact/signals';

import { ApiClient, ApiError } from './api';
import { initialLive, type LiveState, reduceLive, withFeed } from './live';
import type { WsMessage } from './types';

export const api = new ApiClient();
export const live = signal<LiveState>(initialLive);
export const connected = signal(false);
export const loggedIn = signal(api.loggedIn);
export const toast = signal<{ text: string; error: boolean } | null>(null);

api.onUnauthorized = () => (loggedIn.value = false);

let toastTimer: ReturnType<typeof setTimeout> | undefined;
export function notify(text: string, error = false) {
  toast.value = { text, error };
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => (toast.value = null), error ? 5000 : 2500);
}

// Runs an API action and reports failures as a toast. Returns undefined on error.
export async function act<T>(fn: () => Promise<T>, success?: string): Promise<T | undefined> {
  try {
    const r = await fn();
    if (success) notify(success);
    return r;
  } catch (e) {
    notify(e instanceof ApiError ? e.message : 'Network error: is the costume reachable?', true);
    return undefined;
  }
}

export function applyMessage(msg: WsMessage) {
  live.value = reduceLive(live.value, msg);
}

// WebSocket with automatic reconnect. On (re)connect the server sends "hello" with status
// and board; we also refetch the tap feed so nothing is missed while disconnected.
export function connectLive(url = defaultWsUrl()) {
  let delay = 500;
  let socket: WebSocket | null = null;
  let stopped = false;

  const open = () => {
    socket = new WebSocket(url);
    socket.onopen = async () => {
      connected.value = true;
      delay = 500;
      try {
        // Await first: "hello" usually arrives while this request is in flight, and reading
        // live.value before the await would overwrite it with stale state.
        const taps = await api.taps(30);
        live.value = withFeed(live.value, taps);
      } catch {
        /* feed is best-effort */
      }
    };
    socket.onmessage = (e) => {
      try {
        applyMessage(JSON.parse(e.data as string) as WsMessage);
      } catch {
        /* ignore malformed */
      }
    };
    socket.onclose = () => {
      connected.value = false;
      if (!stopped) setTimeout(open, (delay = Math.min(delay * 2, 8000)));
    };
  };
  open();
  return () => {
    stopped = true;
    socket?.close();
  };
}

function defaultWsUrl(): string {
  const proto = location.protocol === 'https:' ? 'wss' : 'ws';
  return `${proto}://${location.host}/ws`;
}
