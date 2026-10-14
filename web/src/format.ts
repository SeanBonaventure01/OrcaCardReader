import type { GameState, Outcome } from './types';

// "m:ss", rounding up so a countdown only shows 0:00 when it's done (matches the firmware).
export function mmss(ms: number): string {
  const total = Math.max(0, Math.ceil(ms / 1000));
  return `${Math.floor(total / 60)}:${String(total % 60).padStart(2, '0')}`;
}

// Human duration for settings: 300000 -> "5 min", 90000 -> "1 min 30 s", 500 -> "0.5 s".
export function humanDuration(ms: number): string {
  if (ms < 1000) return `${ms / 1000} s`;
  const s = Math.round(ms / 1000);
  const m = Math.floor(s / 60);
  const rest = s % 60;
  if (m === 0) return `${rest} s`;
  return rest ? `${m} min ${rest} s` : `${m} min`;
}

export function multiplierLabel(m: number): string {
  return Number.isInteger(m) ? `${m}x` : `${m.toFixed(1)}x`;
}

export const stateLabel: Record<GameState, string> = {
  none: 'No game',
  ready: 'Ready',
  running: 'Running',
  paused: 'Paused',
  ended: 'Ended',
};

export type Tone = 'success' | 'bonus' | 'warning' | 'error' | 'neutral';

export function outcomeTone(o: Outcome): Tone {
  switch (o) {
    case 'accepted':
      return 'success';
    case 'bonus':
      return 'bonus';
    case 'cooldown':
      return 'warning';
    case 'closed':
      return 'neutral';
    default:
      return 'error';
  }
}

// Which lifecycle buttons make sense in each state.
export function availableActions(state: GameState): ('start' | 'pause' | 'resume' | 'stop' | 'reset')[] {
  switch (state) {
    case 'ready':
      return ['start'];
    case 'running':
      return ['pause', 'stop'];
    case 'paused':
      return ['resume', 'stop'];
    case 'ended':
      return ['reset'];
    default:
      return [];
  }
}

// Random 7-byte NTAG-style UID for simulated new cards.
export function randomUid(rand: () => number = Math.random): string {
  let s = '04';
  for (let i = 0; i < 6; i++) s += Math.floor(rand() * 256).toString(16).padStart(2, '0').toUpperCase();
  return s;
}
