import type { Board, EventKind, EventWindow, GameInfo, Player, ProvisionRecord, Provisioning, Settings, Status, TapRecord } from './types';

export class ApiError extends Error {
  constructor(
    public status: number,
    message: string,
  ) {
    super(message);
  }
}

type Fetch = typeof fetch;

// Thin typed client for the costume's REST API. Throws ApiError for non-2xx responses.
// There's no login: only phones on the operator's hotspot can reach the costume.
export class ApiClient {
  constructor(
    private readonly fetchImpl: Fetch = (...args) => fetch(...args),
    private readonly base = '',
  ) {}

  async request<T>(method: string, path: string, body?: unknown): Promise<T> {
    const headers: Record<string, string> = {};
    if (body !== undefined) headers['Content-Type'] = 'application/json';
    const res = await this.fetchImpl(this.base + path, {
      method,
      headers,
      body: body === undefined ? undefined : JSON.stringify(body),
    });
    const text = await res.text();
    let data: unknown = null;
    try {
      data = text ? JSON.parse(text) : null;
    } catch {
      throw new ApiError(res.status, `bad response from ${path}`);
    }
    if (!res.ok) {
      const message = (data as { error?: string } | null)?.error ?? `HTTP ${res.status}`;
      throw new ApiError(res.status, message);
    }
    return data as T;
  }

  // ---- public
  health = () => this.request<{ ok: boolean }>('GET', '/api/health');
  status = () => this.request<Status>('GET', '/api/status');
  board = (limit?: number) => this.request<Board>('GET', limit ? `/api/board?limit=${limit}` : '/api/board');
  taps = (limit = 20) => this.request<{ taps: TapRecord[] }>('GET', `/api/taps?limit=${limit}`).then((r) => r.taps);

  // ---- operator
  games = () => this.request<{ games: GameInfo[] }>('GET', '/api/games').then((r) => r.games);
  selectGame = (id: string, params: Record<string, unknown> = {}, keepScores = false) =>
    this.request<Status>('POST', '/api/game/select', {
      id,
      ...(Object.keys(params).length ? { params } : {}),
      ...(keepScores ? { keep_scores: true } : {}),
    });
  gameAction = (action: 'start' | 'pause' | 'resume' | 'stop' | 'reset') =>
    this.request<Status>('POST', `/api/game/${action}`);
  // A game-specific action, e.g. Lottery's "draw". Returns the game's result object.
  gameActionCustom = (action: string, args?: Record<string, unknown>) =>
    this.request<Record<string, unknown>>('POST', '/api/game/action', args ? { action, args } : { action });
  players = () => this.request<{ players: Player[] }>('GET', '/api/players').then((r) => r.players);
  enroll = (uid: string, name?: string) =>
    this.request<Player>('POST', '/api/players', name ? { uid, name } : { uid });
  updatePlayer = (no: number, patch: Partial<Pick<Player, 'name' | 'team' | 'banned'>>) =>
    this.request<Player>('PATCH', `/api/players/${no}`, patch);
  // Removes every rider and score (not while a game is running).
  clearPlayers = () => this.request<{ players: Player[] }>('DELETE', '/api/players').then((r) => r.players);
  adjustScore = (no: number, delta: number) => this.request<Player>('POST', `/api/players/${no}/adjust`, { delta });
  triggerEvent = (e: { kind?: EventKind; name?: string; duration_ms: number; multiplier?: number }) =>
    this.request<EventWindow>('POST', '/api/events/trigger', e);
  endEvents = () => this.request<Status>('POST', '/api/events/end');
  // While programming cards, a simulated tap programs the card instead of playing.
  simTap = (target: ({ uid: string } | { player: number }) & { card?: number }) =>
    this.request<TapRecord | { provision: ProvisionRecord }>('POST', '/api/sim/tap', target);
  startProvisioning = (opts: { start?: number; overwrite?: boolean } = {}) =>
    this.request<Provisioning>('POST', '/api/provision', opts);
  stopProvisioning = () => this.request<Provisioning>('POST', '/api/provision/stop');
  setEnrollment = (open: boolean) => this.request<Status>('POST', '/api/enrollment', { open });
  // The costume has no real-time clock; the phone tells it the time and its time zone.
  syncClock = (epochMs = Date.now(), utcOffsetMin = -new Date(epochMs).getTimezoneOffset()) =>
    this.request('POST', '/api/clock', { epoch_ms: epochMs, utc_offset_min: utcOffsetMin });
  playSound = (id: string) => this.request('POST', '/api/fx/sound', { id });
  settings = () => this.request<Settings>('GET', '/api/settings');
  patchSettings = (patch: Partial<Settings>) => this.request<Settings>('PATCH', '/api/settings', patch);
}
