// Shapes of the costume's API (firmware/components/api). Kept in sync by the contract tests,
// which feed the shared fixtures in host/tests/api_fixtures through these types.

export type GameState = 'none' | 'ready' | 'running' | 'paused' | 'ended';
export type Outcome = 'accepted' | 'bonus' | 'cooldown' | 'rejected' | 'unknown' | 'banned' | 'closed';
export type EventKind = 'multiplier' | 'cooldown_waiver' | 'custom';

export interface EventWindow {
  id: number;
  kind: EventKind;
  name: string;
  multiplier: number;
  start: number;
  end: number;
  tag: string;
  random: boolean;
  remaining_ms?: number;
}

export interface Status {
  state: GameState;
  game: { id: string; name: string; params: Record<string, unknown>; status: Record<string, unknown> } | null;
  players: number;
  open_enrollment: boolean;
  events: EventWindow[];
  next_event_in_ms: number | null;
  taps: number;
  total_points: number;
  time_ms: number;
  wall_clock_ms: number | null;
  provisioning: Provisioning;
}

export type ProvisionResult = 'written' | 'already' | 'adopted' | 'failed' | 'unsupported';

export interface ProvisionRecord {
  uid: string;
  number: number;
  result: ProvisionResult;
  message: string;
}

// Card programming: while active, taps write player numbers onto cards.
export interface Provisioning {
  active: boolean;
  next: number;
  overwrite: boolean;
  programmed: number;
  last: ProvisionRecord | null;
}

export interface TapRecord {
  seq: number;
  t: number;
  wall_ms: number;
  uid: string;
  player: number;
  name: string;
  outcome: Outcome;
  points: number;
  balance: number;
  multiplier: number;
  headline: string;
  detail: string;
}

export interface BoardRow {
  rank: number;
  no: number;
  name: string;
  team: string;
  points: number;
}

export interface Board {
  board: BoardRow[];
  teams: { team: string; points: number }[];
}

export type ParamType = 'int' | 'float' | 'bool' | 'enum' | 'duration' | 'string';

export interface ParamSpec {
  key: string;
  label: string;
  type: ParamType;
  default: unknown;
  min?: number;
  max?: number;
  options?: string[];
  max_length?: number;
  help?: string;
}

export interface GameAction {
  id: string;
  label: string;
  help: string;
}

export interface GameInfo {
  id: string;
  name: string;
  description: string;
  params: ParamSpec[];
  actions: GameAction[];
}

export interface Player {
  no: number;
  uid: string;
  name: string;
  team: string;
  banned: boolean;
  points: number;
  taps?: number;
  accepted?: number;
}

export interface Settings {
  volume: number;
  brightness: number;
  led_brightness: number;
  led_count: number;
  led_max_ma: number;
  voice: VoicePack;
  effects: boolean;
}

// Voice lines after the tap sound. "profanity" is for grown-up parties.
export type VoicePack = 'off' | 'profanity';

// Simulated hardware output (orca-sim only): what the costume's screen/speaker/LEDs did.
export interface DeviceOutput {
  out: 'screen' | 'sound' | 'leds';
  kind?: string;
  tone?: string;
  headline?: string;
  detail?: string;
  player?: string;
  note?: string;
  id?: string;
  effect?: string;
}

export type WsMessage =
  | { type: 'hello'; status: Status; board: Board }
  | { type: 'state'; status: Status }
  | { type: 'tap'; tap: TapRecord }
  | ({ type: 'scores' } & Board)
  | { type: 'event'; action: 'start' | 'end'; event: EventWindow }
  | { type: 'players' }
  | { type: 'provision'; record: ProvisionRecord }
  | { type: 'announce'; headline: string; detail: string }
  | ({ type: 'device' } & DeviceOutput);
