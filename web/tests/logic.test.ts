import { describe, expect, it } from 'vitest';

import { availableActions, humanDuration, mmss, multiplierLabel, outcomeTone, randomUid } from '../src/format';
import { engineNow, FEED_LIMIT, initialLive, PROGRAMMED_LIMIT, reduceLive } from '../src/live';
import { fromFormValues, toFormValues } from '../src/paramForm';
import type { GameInfo, Status, TapRecord } from '../src/types';
import { loadFixtures, materialize } from './fixtures';

const fixtures = loadFixtures();
const games = (materialize(fixtures.get('games_list')!.response.body) as { games: GameInfo[] }).games;
const farePaid = games.find((g) => g.id === 'fare_paid')!;

describe('format', () => {
  it('mmss rounds up like the firmware', () => {
    expect(mmss(0)).toBe('0:00');
    expect(mmss(1)).toBe('0:01');
    expect(mmss(192000)).toBe('3:12');
    expect(mmss(299999)).toBe('5:00');
    expect(mmss(-5)).toBe('0:00');
  });
  it('humanDuration', () => {
    expect(humanDuration(300000)).toBe('5 min');
    expect(humanDuration(90000)).toBe('1 min 30 s');
    expect(humanDuration(10000)).toBe('10 s');
    expect(humanDuration(0)).toBe('0 s');
  });
  it('multiplierLabel', () => {
    expect(multiplierLabel(2)).toBe('2x');
    expect(multiplierLabel(1.5)).toBe('1.5x');
  });
  it('actions per state', () => {
    expect(availableActions('none')).toEqual([]);
    expect(availableActions('ready')).toEqual(['start']);
    expect(availableActions('running')).toEqual(['pause', 'stop']);
    expect(availableActions('paused')).toEqual(['resume', 'stop']);
    expect(availableActions('ended')).toEqual(['reset']);
  });
  it('outcome tones', () => {
    expect(outcomeTone('accepted')).toBe('success');
    expect(outcomeTone('bonus')).toBe('bonus');
    expect(outcomeTone('cooldown')).toBe('warning');
    expect(outcomeTone('banned')).toBe('error');
    expect(outcomeTone('closed')).toBe('neutral');
  });
  it('randomUid is a 7-byte hex UID', () => {
    expect(randomUid(() => 0.5)).toBe('04808080808080');
    expect(randomUid()).toMatch(/^04[0-9A-F]{12}$/);
  });
});

describe('paramForm (schema from the games_list fixture)', () => {
  it('fills defaults, durations as minutes and seconds', () => {
    const v = toFormValues(farePaid.params);
    expect(v['cooldown.min']).toBe('5');
    expect(v['cooldown.sec']).toBe('0');
    expect(v.points).toBe('1');
  });

  it('prefers current params over defaults', () => {
    const v = toFormValues(farePaid.params, { cooldown: 90000, points: 3 });
    expect(v['cooldown.min']).toBe('1');
    expect(v['cooldown.sec']).toBe('30');
    expect(v.points).toBe('3');
  });

  it('round-trips to the params the API expects', () => {
    const r = fromFormValues(farePaid.params, toFormValues(farePaid.params, { cooldown: 90000, points: 3 }));
    expect(r.errors).toEqual({});
    expect(r.params).toEqual({ cooldown: 90000, points: 3 });
  });

  it('validates like the firmware', () => {
    const base = toFormValues(farePaid.params);
    expect(fromFormValues(farePaid.params, { ...base, points: '0' }).errors.points).toMatch(/between 1 and 100/);
    expect(fromFormValues(farePaid.params, { ...base, points: '1.5' }).errors.points).toMatch(/whole number/);
    expect(fromFormValues(farePaid.params, { ...base, points: '' }).errors.points).toBeDefined();
    expect(fromFormValues(farePaid.params, { ...base, 'cooldown.min': '61' }).errors.cooldown).toMatch(/between 0:00 and 60:00/);
    expect(fromFormValues(farePaid.params, { ...base, 'cooldown.sec': 'x' }).errors.cooldown).toBeDefined();
    expect(fromFormValues(farePaid.params, { ...base, 'cooldown.min': '', 'cooldown.sec': '' }).params.cooldown).toBe(0);
  });

  it('handles float params (rush hour multiplier)', () => {
    const rush = games.find((g) => g.id === 'rush_hour')!;
    const v = toFormValues(rush.params);
    expect(fromFormValues(rush.params, { ...v, multiplier: '2.5' }).params.multiplier).toBe(2.5);
    expect(fromFormValues(rush.params, { ...v, multiplier: '0.5' }).errors.multiplier).toBeDefined();
  });

  it('handles bool, enum and string params', () => {
    const schema = [
      { key: 'on', label: 'On', type: 'bool' as const, default: true },
      { key: 'mode', label: 'Mode', type: 'enum' as const, default: 'a', options: ['a', 'b'] },
      { key: 'label', label: 'Label', type: 'string' as const, default: 'x', max_length: 3 },
    ];
    const v = toFormValues(schema);
    expect(fromFormValues(schema, v).params).toEqual({ on: true, mode: 'a', label: 'x' });
    expect(fromFormValues(schema, { ...v, mode: 'z', label: 'long' }).errors).toEqual({
      mode: 'Pick one of the options',
      label: 'At most 3 characters',
    });
  });
});

describe('gameStatusLine', () => {
  const base = materialize(fixtures.get('status_initial')!.response.body) as Status;
  const withGame = (status: Record<string, unknown>): Status => ({
    ...base,
    game: { id: 'x', name: 'X', params: {}, status },
  });
  it('describes each game that has its own state', async () => {
    const { gameStatusLine } = await import('../src/components/LivePage');
    expect(gameStatusLine(base)).toBeNull();
    expect(gameStatusLine(withGame({ holder_name: 'Ghost' }))).toBe('On the bus: Ghost');
    expect(gameStatusLine(withGame({ holder: null, holder_name: null }))).toBeNull();
    expect(gameStatusLine(withGame({ inspecting: true, checked: 3 }))).toBe('Fare inspection in progress (3 checked)');
    expect(gameStatusLine(withGame({ inspecting: false }))).toBeNull();
    expect(gameStatusLine(withGame({ winners: [{ no: 1, name: 'A' }, { no: 4, name: 'B' }] }))).toBe('Winners: #1 A, #4 B');
    expect(gameStatusLine(withGame({ winners: [] }))).toBeNull();
  });
});

describe('live reducer', () => {
  const status = materialize(fixtures.get('status_initial')!.response.body) as Status;
  const tap = materialize(fixtures.get('sim_tap_accepted')!.response.body) as TapRecord;

  it('hello sets status, board and arrival time', () => {
    const s = reduceLive(initialLive, { type: 'hello', status, board: { board: [], teams: [] } }, 5000);
    expect(s.status?.state).toBe('none');
    expect(s.statusAt).toBe(5000);
    expect(engineNow(s, 6500)).toBe(status.time_ms + 1500);
  });

  it('taps go newest-first, de-duplicated and capped', () => {
    let s = reduceLive(initialLive, { type: 'hello', status, board: { board: [], teams: [] } });
    for (let i = 1; i <= FEED_LIMIT + 5; i++) s = reduceLive(s, { type: 'tap', tap: { ...tap, seq: i } });
    s = reduceLive(s, { type: 'tap', tap: { ...tap, seq: FEED_LIMIT + 5 } });
    expect(s.feed).toHaveLength(FEED_LIMIT);
    expect(s.feed[0].seq).toBe(FEED_LIMIT + 5);
    expect(s.status?.taps).toBe(FEED_LIMIT + 5);
  });

  it('events start and end', () => {
    const ev = materialize(fixtures.get('trigger_event')!.response.body) as never;
    let s = reduceLive(initialLive, { type: 'state', status });
    s = reduceLive(s, { type: 'event', action: 'start', event: ev });
    expect(s.status?.events).toHaveLength(1);
    s = reduceLive(s, { type: 'event', action: 'start', event: ev });
    expect(s.status?.events).toHaveLength(1);
    s = reduceLive(s, { type: 'event', action: 'end', event: ev });
    expect(s.status?.events).toHaveLength(0);
  });

  it('events before any status are ignored', () => {
    const ev = materialize(fixtures.get('trigger_event')!.response.body) as never;
    expect(reduceLive(initialLive, { type: 'event', action: 'start', event: ev })).toBe(initialLive);
  });

  it('scores, players, announcements and device output', () => {
    const board = materialize(fixtures.get('board')!.response.body) as { board: never[]; teams: never[] };
    let s = reduceLive(initialLive, { type: 'scores', ...board });
    expect(s.board.board).toHaveLength(2);
    s = reduceLive(s, { type: 'players' });
    expect(s.playersVersion).toBe(1);
    s = reduceLive(s, { type: 'announce', headline: 'Rush hour', detail: '2x' }, 42);
    expect(s.announcement).toEqual({ headline: 'Rush hour', detail: '2x', at: 42 });
    s = reduceLive(s, { type: 'device', out: 'screen', headline: 'Fare paid', tone: 'success' });
    s = reduceLive(s, { type: 'device', out: 'leds', effect: 'accept_flash' });
    s = reduceLive(s, { type: 'device', out: 'sound', id: 'accept' });
    expect(s.device).toMatchObject({ seen: true, leds: 'accept_flash', sound: 'accept' });
    expect(s.device.screen?.headline).toBe('Fare paid');
  });
});

describe('card programming', () => {
  const status = materialize(fixtures.get('provision_status')!.response.body) as Status;

  it('status carries the provisioning state from the firmware', () => {
    expect(status.provisioning.active).toBe(true);
    expect(status.provisioning.next).toBe(2);
    expect(status.provisioning.last?.result).toBe('written');
  });

  it('provision messages build a newest-first log with a limit', () => {
    const record = status.provisioning.last!;
    let s = initialLive;
    for (let i = 1; i <= PROGRAMMED_LIMIT + 3; i++) s = reduceLive(s, { type: 'provision', record: { ...record, number: i } });
    expect(s.programmed).toHaveLength(PROGRAMMED_LIMIT);
    expect(s.programmed[0].number).toBe(PROGRAMMED_LIMIT + 3);
  });
});
