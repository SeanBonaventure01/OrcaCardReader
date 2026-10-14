// The phone UI's API client against the shared contract fixtures. The same fixtures are
// replayed against the real C++ API, so if either side drifts, one of the suites fails.
import { describe, expect, it } from 'vitest';

import { ApiClient, ApiError, memoryTokenStore } from '../src/api';
import { type Fixture, fixtureFetch, loadFixtures, materialize, type RecordedRequest } from './fixtures';

const fixtures = loadFixtures();

// How the UI makes each fixture's request.
const calls: Record<string, (c: ApiClient) => Promise<unknown>> = {
  login_ok: (c) => c.login('1031'),
  login_wrong_pin: (c) => c.login('0000'),
  admin_route_without_login: (c) => c.games(),
  status_initial: (c) => c.status(),
  games_list: (c) => c.games(),
  select_game: (c) => c.selectGame('fare_paid', { cooldown: 60000 }),
  select_game_bad_param: (c) => c.selectGame('fare_paid', { points: 0 }),
  select_unknown_game: (c) => c.selectGame('pinball', {}),
  start_game: (c) => c.gameAction('start'),
  start_without_game: (c) => c.gameAction('start'),
  sim_tap_accepted: (c) => c.simTap({ uid: '04A1B2C3D4E5F6' }),
  sim_tap_cooldown: (c) => c.simTap({ uid: '04A1B2C3D4E5F6' }),
  sim_tap_closed: (c) => c.simTap({ uid: '04A1B2C3D4E5F6' }),
  taps_feed: (c) => c.taps(5),
  board: (c) => c.board(),
  stop_game: (c) => c.gameAction('stop'),
  lottery_draw: (c) => c.gameActionCustom('draw'),
  game_action_unknown: (c) => c.gameActionCustom('draw'),
  pause_when_not_running: (c) => c.gameAction('pause'),
  players_empty: (c) => c.players(),
  enroll_player: (c) => c.enroll('04:a1:b2:c3', 'Ghost'),
  enroll_duplicate: (c) => c.enroll('04A1B2C3'),
  players_list: (c) => c.players(),
  patch_player: (c) => c.updatePlayer(1, { name: 'Pumpkin', team: 'Link', banned: true }),
  // Deliberately invalid field to check the error path.
  patch_player_bad_field: (c) => c.updatePlayer(1, { colour: 'orange' } as never),
  adjust_score: (c) => c.adjustScore(1, -3),
  trigger_event: (c) => c.triggerEvent({ name: 'Rush hour', duration_ms: 120000, multiplier: 3 }),
  status_during_event: (c) => c.status(),
  trigger_event_bad_kind: (c) => c.triggerEvent({ kind: 'earthquake' as never, duration_ms: 1000 }),
  trigger_event_not_running: (c) => c.triggerEvent({ duration_ms: 1000 }),
  settings_get: (c) => c.settings(),
  settings_patch: (c) => c.patchSettings({ volume: 35 }),
  settings_patch_invalid: (c) => c.patchSettings({ brightness: 0 }),
  enrollment_close: (c) => c.setEnrollment(false),
  clock_sync: (c) => c.syncClock(1793476920000, -420),
  unknown_route: (c) => c.request('GET', '/api/nope'),
};

async function run(fx: Fixture) {
  const sent: RecordedRequest[] = [];
  const auth = fx.request.auth ?? true;
  const client = new ApiClient(fixtureFetch(fx, sent), memoryTokenStore(auth ? 'tok' : null));
  let result: unknown;
  let error: unknown;
  try {
    result = await calls[fx.name](client);
  } catch (e) {
    error = e;
  }
  return { sent, result, error, client };
}

describe('API contract fixtures', () => {
  it('has a UI call for every fixture', () => {
    expect([...fixtures.keys()].filter((n) => !calls[n])).toEqual([]);
  });

  for (const fx of fixtures.values()) {
    if (!calls[fx.name]) continue;
    it(fx.name, async () => {
      const { sent, error } = await run(fx);
      // Request: exactly what the fixture (verified against the firmware) describes.
      expect(sent).toHaveLength(1);
      expect(sent[0].method).toBe(fx.request.method);
      expect(sent[0].path).toBe(fx.request.path);
      expect(sent[0].body).toEqual(fx.request.body);
      expect(sent[0].auth).toBe((fx.request.auth ?? true) ? 'Bearer tok' : null);

      // Response: success values come through; errors become ApiError with the server's message.
      const body = materialize(fx.response.body) as Record<string, unknown>;
      if (fx.response.status >= 400) {
        expect(error).toBeInstanceOf(ApiError);
        expect((error as ApiError).status).toBe(fx.response.status);
        expect((error as ApiError).message).toBe(body.error);
      } else {
        expect(error).toBeUndefined();
      }
    });
  }

  it('login stores the token from the response', async () => {
    const { client } = await run(fixtures.get('login_ok')!);
    expect(client.tokens.get()).toBe('sample');
  });

  it('a 401 on an admin call logs out', async () => {
    const fx = fixtures.get('admin_route_without_login')!;
    const sent: RecordedRequest[] = [];
    const client = new ApiClient(fixtureFetch(fx, sent), memoryTokenStore('stale'));
    let fired = false;
    client.onUnauthorized = () => (fired = true);
    await expect(client.games()).rejects.toBeInstanceOf(ApiError);
    expect(client.tokens.get()).toBeNull();
    expect(fired).toBe(true);
  });

  it('wrong PIN does not count as being logged out', async () => {
    const { client } = await run(fixtures.get('login_wrong_pin')!);
    expect(client.tokens.get()).toBeNull();
  });

  it('unwraps list responses', async () => {
    expect(((await run(fixtures.get('players_list')!)).result as unknown[]).length).toBe(2);
    expect(((await run(fixtures.get('games_list')!)).result as unknown[]).length).toBe(6);
    expect(((await run(fixtures.get('taps_feed')!)).result as unknown[]).length).toBe(2);
  });
});
