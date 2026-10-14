import { useEffect, useState } from 'preact/hooks';

import { availableActions, mmss, multiplierLabel, outcomeTone, randomUid, stateLabel } from '../format';
import { engineNow } from '../live';
import { act, api, live } from '../store';
import type { Player } from '../types';
import { Card, EventBadge, Leaderboard, useNow } from './common';

const actionLabel = { start: 'Start', pause: 'Pause', resume: 'Resume', stop: 'Stop', reset: 'Reset scores' } as const;

function GameCard() {
  const s = live.value.status;
  const now = useNow();
  if (!s) return <Card title="Game">Connecting…</Card>;
  const tNow = engineNow(live.value, now);
  return (
    <Card title="Game" testId="game-card">
      <div class="game-line">
        <strong data-testid="game-name">{s.game?.name ?? 'No game selected'}</strong>
        <span class={`chip state-${s.state}`} data-testid="game-state">
          {stateLabel[s.state]}
        </span>
      </div>
      <div class="stats">
        <span>
          <b>{s.players}</b> riders
        </span>
        <span>
          <b>{s.taps}</b> taps
        </span>
        <span>
          <b>{s.total_points}</b> pts
        </span>
      </div>
      {s.events.length > 0 && (
        <div class="events">
          {s.events.map((e) => (
            <EventBadge key={e.id} name={e.name} kind={e.kind} multiplier={e.multiplier} remaining={mmss(e.end - tNow)} />
          ))}
        </div>
      )}
      {s.state === 'running' && s.next_event_in_ms !== null && s.events.length === 0 && (
        <p class="muted">Next surprise event in about {mmss(s.next_event_in_ms - (now - live.value.statusAt))}</p>
      )}
      <div class="row">
        {availableActions(s.state).map((a) => (
          <button
            key={a}
            class={a === 'stop' || a === 'reset' ? 'danger' : 'primary'}
            data-testid={`action-${a}`}
            onClick={() => act(() => api.gameAction(a))}
          >
            {actionLabel[a]}
          </button>
        ))}
        {s.state === 'none' && <a href="#/games">Pick a game</a>}
      </div>
    </Card>
  );
}

function QuickEvents() {
  const running = live.value.status?.state === 'running';
  const trigger = (body: Parameters<typeof api.triggerEvent>[0], label: string) =>
    act(() => api.triggerEvent(body), `${label} started`);
  return (
    <Card title="Quick events">
      <div class="row">
        <button disabled={!running} data-testid="quick-rush" onClick={() => trigger({ name: 'RUSH HOUR', duration_ms: 120000, multiplier: 2 }, 'Rush hour')}>
          Rush hour 2x · 2 min
        </button>
        <button disabled={!running} onClick={() => trigger({ name: 'SURGE', duration_ms: 60000, multiplier: 3 }, 'Surge')}>
          Surge 3x · 1 min
        </button>
        <button
          disabled={!running}
          onClick={() => trigger({ kind: 'cooldown_waiver', name: 'FREE RIDE', duration_ms: 60000 }, 'Free ride')}
        >
          Free ride · 1 min
        </button>
      </div>
      {!running && <p class="muted">Start a game to trigger events.</p>}
    </Card>
  );
}

// Tap without a physical card (testing, or when a guest lost theirs).
function SimTap() {
  const [players, setPlayers] = useState<Player[]>([]);
  const [choice, setChoice] = useState('new');
  const version = live.value.playersVersion;
  useEffect(() => {
    api.players().then(setPlayers).catch(() => {});
  }, [version]);
  const tap = () =>
    act(() => api.simTap(choice === 'new' ? { uid: randomUid() } : { player: Number(choice) }));
  return (
    <Card title="Simulate a tap">
      <div class="row">
        <select aria-label="Card to tap" value={choice} onChange={(e) => setChoice((e.target as HTMLSelectElement).value)}>
          <option value="new">New card</option>
          {players.map((p) => (
            <option key={p.no} value={String(p.no)}>
              #{p.no} {p.name}
            </option>
          ))}
        </select>
        <button class="primary" data-testid="sim-tap" onClick={tap}>
          Tap
        </button>
      </div>
    </Card>
  );
}

function Feed() {
  const feed = live.value.feed;
  return (
    <Card title="Live taps" testId="feed">
      {feed.length === 0 ? (
        <p class="muted">No taps yet.</p>
      ) : (
        <ul class="feed">
          {feed.map((t) => (
            <li key={t.seq} class={`tone-${outcomeTone(t.outcome)}`} data-testid="feed-item">
              <span class="headline">{t.headline}</span>
              <span class="who">{t.player ? `#${t.player} ${t.name}` : t.uid}</span>
              <span class="pts">
                {t.points > 0 ? `+${t.points}` : t.points !== 0 ? t.points : ''}
                {t.multiplier > 1 && t.points > 0 ? ` (${multiplierLabel(t.multiplier)})` : ''}
              </span>
            </li>
          ))}
        </ul>
      )}
    </Card>
  );
}

// Shows what the costume itself is doing (only when connected to orca-sim).
function DevicePreview() {
  const d = live.value.device;
  if (!d.seen) return null;
  return (
    <Card title="Costume (simulated)" testId="device-preview">
      <div class={`device-screen tone-${d.screen?.tone ?? 'neutral'}`} data-testid="device-screen">
        <div class="dev-headline">{d.screen?.headline || 'TAP CARD HERE'}</div>
        <div class="dev-detail">{d.screen?.detail}</div>
        <div class="dev-player">{d.screen?.player}</div>
      </div>
      <p class="muted">
        LEDs: {d.leds ?? 'off'} · last sound: {d.sound ?? 'none'}
      </p>
    </Card>
  );
}

export function LivePage() {
  return (
    <div class="grid">
      <GameCard />
      <DevicePreview />
      <QuickEvents />
      <SimTap />
      <Feed />
      <Card title="Leaderboard">
        <Leaderboard board={live.value.board} limit={10} />
      </Card>
    </div>
  );
}
