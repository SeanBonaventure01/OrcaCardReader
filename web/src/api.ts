import type { Board, EventKind, EventWindow, GameInfo, Player, Settings, Status, TapRecord } from './types';

export class ApiError extends Error {
  constructor(
    public status: number,
    message: string,
  ) {
    super(message);
  }
}

type Fetch = typeof fetch;

export interface TokenStore {
  get(): string | null;
  set(token: string | null): void;
}

export const localTokenStore: TokenStore = {
  get: () => {
    try {
      return localStorage.getItem('orca_token');
    } catch {
      return null;
    }
  },
  set: (t) => {
    try {
      if (t) localStorage.setItem('orca_token', t);
      else localStorage.removeItem('orca_token');
    } catch {
      /* private mode: token lives for this page only */
    }
  },
};

export function memoryTokenStore(initial: string | null = null): TokenStore {
  let token = initial;
  return { get: () => token, set: (t) => (token = t) };
}

// Thin typed client for the costume's REST API. Throws ApiError for non-2xx responses.
export class ApiClient {
  onUnauthorized: () => void = () => {};

  constructor(
    private readonly fetchImpl: Fetch = (...args) => fetch(...args),
    readonly tokens: TokenStore = localTokenStore,
    private readonly base = '',
  ) {}

  get loggedIn(): boolean {
    return this.tokens.get() !== null;
  }

  async request<T>(method: string, path: string, body?: unknown): Promise<T> {
    const headers: Record<string, string> = {};
    const token = this.tokens.get();
    if (token) headers.Authorization = `Bearer ${token}`;
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
      if (res.status === 401 && path !== '/api/login') {
        this.tokens.set(null);
        this.onUnauthorized();
      }
      throw new ApiError(res.status, message);
    }
    return data as T;
  }

  // ---- public
  status = () => this.request<Status>('GET', '/api/status');
  board = (limit?: number) => this.request<Board>('GET', limit ? `/api/board?limit=${limit}` : '/api/board');
  taps = (limit = 20) => this.request<{ taps: TapRecord[] }>('GET', `/api/taps?limit=${limit}`).then((r) => r.taps);

  async login(pin: string): Promise<void> {
    const r = await this.request<{ token: string }>('POST', '/api/login', { pin });
    this.tokens.set(r.token);
  }

  async logout(): Promise<void> {
    try {
      await this.request('POST', '/api/logout');
    } finally {
      this.tokens.set(null);
    }
  }

  // ---- admin
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
  adjustScore = (no: number, delta: number) => this.request<Player>('POST', `/api/players/${no}/adjust`, { delta });
  triggerEvent = (e: { kind?: EventKind; name?: string; duration_ms: number; multiplier?: number }) =>
    this.request<EventWindow>('POST', '/api/events/trigger', e);
  endEvents = () => this.request<Status>('POST', '/api/events/end');
  simTap = (target: { uid: string } | { player: number }) => this.request<TapRecord>('POST', '/api/sim/tap', target);
  setEnrollment = (open: boolean) => this.request<Status>('POST', '/api/enrollment', { open });
  syncClock = (epochMs = Date.now()) => this.request('POST', '/api/clock', { epoch_ms: epochMs });
  playSound = (id: string) => this.request('POST', '/api/fx/sound', { id });
  settings = () => this.request<Settings>('GET', '/api/settings');
  patchSettings = (patch: Partial<Settings>) => this.request<Settings>('PATCH', '/api/settings', patch);
}
