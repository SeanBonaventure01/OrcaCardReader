import { signal } from '@preact/signals';

import { ApiClient, ApiError } from './api';
import { initialLive, type LiveState, reduceLive, withFeed } from './live';
import type { WsMessage } from './types';

export const api = new ApiClient();
export const live = signal<LiveState>(initialLive);
export const connected = signal(false);
export const toast = signal<{ text: string; error: boolean } | null>(null);

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

// Status, board and tap feed over plain HTTP: on load, and every few seconds while the
// WebSocket is down, so the controls work even when live updates don't.
export async function refreshOverHttp() {
  try {
    const [status, board, taps] = await Promise.all([api.status(), api.board(10), api.taps(30)]);
    live.value = withFeed(reduceLive(reduceLive(live.value, { type: 'state', status }), { type: 'scores', ...board }), taps);
  } catch {
    /* costume unreachable; the next poll tries again */
  }
}

const POLL_MS = 3000;
const CONNECT_TIMEOUT_MS = 5000;

// WebSocket with automatic reconnect. On (re)connect the server sends "hello" with status
// and board; we also refetch the tap feed so nothing is missed while disconnected. While it's
// down, HTTP polling keeps the page current.
export function connectLive(url = defaultWsUrl()) {
  let delay = 500;
  let socket: WebSocket | null = null;
  let stopped = false;

  const poll = setInterval(() => {
    if (!connected.value) refreshOverHttp();
  }, POLL_MS);
  refreshOverHttp();

  const open = () => {
    const ws = new WebSocket(url);
    socket = ws;
    // A handshake that stalls can take the browser minutes to give up on; don't wait.
    const timeout = setTimeout(() => {
      if (ws.readyState === WebSocket.CONNECTING) ws.close();
    }, CONNECT_TIMEOUT_MS);
    ws.onopen = async () => {
      clearTimeout(timeout);
      connected.value = true;
      delay = 500;
      // After a costume reboot its clock is unset; the operator's phone fixes that on reconnect.
      api.syncClock().catch(() => {});
      try {
        // Await first: "hello" usually arrives while this request is in flight, and reading
        // live.value before the await would overwrite it with stale state.
        const taps = await api.taps(30);
        live.value = withFeed(live.value, taps);
      } catch {
        /* feed is best-effort */
      }
    };
    ws.onmessage = (e) => {
      try {
        applyMessage(JSON.parse(e.data as string) as WsMessage);
      } catch {
        /* ignore malformed */
      }
    };
    ws.onclose = () => {
      clearTimeout(timeout);
      connected.value = false;
      if (!stopped) setTimeout(open, (delay = Math.min(delay * 2, 8000)));
    };
  };
  open();
  return () => {
    stopped = true;
    clearInterval(poll);
    socket?.close();
  };
}

function defaultWsUrl(): string {
  const proto = location.protocol === 'https:' ? 'wss' : 'ws';
  return `${proto}://${location.host}/ws`;
}
