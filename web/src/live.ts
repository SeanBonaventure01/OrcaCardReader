import type { Board, DeviceOutput, ProvisionRecord, Status, TapRecord, WsMessage } from './types';

// Everything the UI shows live, updated from WebSocket push messages by a pure reducer.
export interface LiveState {
  status: Status | null;
  statusAt: number; // Date.now() when `status` arrived, to run countdowns between updates
  board: Board;
  feed: TapRecord[]; // newest first
  announcement: { headline: string; detail: string; at: number } | null;
  device: { screen: DeviceOutput | null; leds: string | null; sound: string | null; seen: boolean };
  playersVersion: number; // bumps when players change, so pages can refetch
  programmed: ProvisionRecord[]; // card programming results, newest first
}

export const FEED_LIMIT = 30;
export const PROGRAMMED_LIMIT = 50;

export const initialLive: LiveState = {
  status: null,
  statusAt: 0,
  board: { board: [], teams: [] },
  feed: [],
  announcement: null,
  device: { screen: null, leds: null, sound: null, seen: false },
  playersVersion: 0,
  programmed: [],
};

export function reduceLive(s: LiveState, msg: WsMessage, now = Date.now()): LiveState {
  switch (msg.type) {
    case 'hello':
      return { ...s, status: msg.status, statusAt: now, board: msg.board };
    case 'state':
      return { ...s, status: msg.status, statusAt: now };
    case 'tap': {
      if (s.feed.some((t) => t.seq === msg.tap.seq)) return s;
      const feed = [msg.tap, ...s.feed].slice(0, FEED_LIMIT);
      const status = s.status ? { ...s.status, taps: Math.max(s.status.taps, msg.tap.seq) } : s.status;
      return { ...s, feed, status };
    }
    case 'scores':
      return { ...s, board: { board: msg.board, teams: msg.teams } };
    case 'event': {
      if (!s.status) return s;
      const others = s.status.events.filter((e) => e.id !== msg.event.id);
      const events = msg.action === 'start' ? [...others, msg.event] : others;
      return { ...s, status: { ...s.status, events } };
    }
    case 'players':
      return { ...s, playersVersion: s.playersVersion + 1 };
    case 'provision':
      return { ...s, programmed: [msg.record, ...s.programmed].slice(0, PROGRAMMED_LIMIT) };
    case 'announce':
      return { ...s, announcement: { headline: msg.headline, detail: msg.detail, at: now } };
    case 'device': {
      const d = { ...s.device, seen: true };
      if (msg.out === 'screen') d.screen = msg;
      else if (msg.out === 'leds') d.leds = msg.effect ?? null;
      else if (msg.out === 'sound') d.sound = msg.id ?? null;
      return { ...s, device: d };
    }
    default:
      return s;
  }
}

// Engine time now, estimated from the last status (engine time advances in real time on the device).
export function engineNow(s: LiveState, now = Date.now()): number {
  return s.status ? s.status.time_ms + (now - s.statusAt) : 0;
}

// Replace the feed with a freshly fetched list (e.g. after reconnecting).
export function withFeed(s: LiveState, taps: TapRecord[]): LiveState {
  return { ...s, feed: taps.slice(0, FEED_LIMIT) };
}
