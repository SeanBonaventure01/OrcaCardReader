import { useEffect, useState } from 'preact/hooks';

import { availableActions, mmss, multiplierLabel, outcomeTone, randomUid, stateLabel } from '../format';
import { engineNow } from '../live';
import { act, api, live, notify } from '../store';
import type { GameInfo, Player, Status } from '../types';
import { Card, EventBadge, Leaderboard, useNow } from './common';

const actionLabel = { start: 'Start', pause: 'Pause', resume: 'Resume', stop: 'Stop', reset: 'Reset scores' } as const;

// One line about the game's own state, for the games that have some.
export function gameStatusLine(s: Status): string | null {
  const g = s.game?.status ?? {};
  if (typeof g.holder_name === 'string') return `On the bus: ${g.holder_name}`;
  if (g.inspecting === true) return `Fare inspection in progress (${String(g.checked ?? 0)} checked)`;
  if (Array.isArray(g.winners) && g.winners.length)
    return `Winners: ${(g.winners as { no: number; name: string }[]).map((w) => `#${w.no} ${w.name}`).join(', ')}`;
  return null;
}

let gamesCache: GameInfo[] | null = null;

function GameActions({ gameId }: { gameId: string }) {
  const [games, setGames] = useState<GameInfo[]>(gamesCache ?? []);
  useEffect(() => {
    if (!gamesCache) api.games().then((g) => setGames((gamesCache = g))).catch(() => {});
  }, []);
  const actions = games.find((g) => g.id === gameId)?.actions ?? [];
  return (
    <>
      {actions.map((a) => (
        <button
          key={a.id}
          class="primary"
          title={a.help}
          data-testid={`game-action-${a.id}`}
          onClick={async () => {
            const r = await act(() => api.gameActionCustom(a.id));
            if (r && typeof r.name === 'string') notify(`Winner: #${String(r.winner)} ${r.name}`);
          }}
        >
          {a.label}
        </button>
      ))}
    </>
  );
}

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
      {gameStatusLine(s) && (
        <p data-testid="game-status-line">
          <strong>{gameStatusLine(s)}</strong>
        </p>
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
        {s.game && s.state !== 'ready' && <GameActions gameId={s.game.id} />}
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
        <button disabled={!running} data-testid="quick-rush" onClick={() => trigger({ name: 'Rush hour', duration_ms: 120000, multiplier: 2 }, 'Rush hour')}>
          Rush hour 2x · 2 min
        </button>
        <button disabled={!running} onClick={() => trigger({ name: 'Surge', duration_ms: 60000, multiplier: 3 }, 'Surge')}>
          Surge 3x · 1 min
        </button>
        <button
          disabled={!running}
          onClick={() => trigger({ kind: 'cooldown_waiver', name: 'Free ride', duration_ms: 60000 }, 'Free ride')}
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

// Name a rider right after they tap: "#07 Rider 7" becomes "#07 Morticia" on the scoreboard.
function NameRider({ no, name, onDone }: { no: number; name: string; onDone: () => void }) {
  const [value, setValue] = useState(/^Rider \d+$/.test(name) ? '' : name);
  const save = async (e: Event) => {
    e.preventDefault();
    const r = await act(() => api.updatePlayer(no, { name: value.trim() }), `#${no} is now ${value.trim()}`);
    if (r) onDone();
  };
  return (
    <form class="row name-rider" onSubmit={save}>
      <input aria-label={`Name for rider ${no}`} placeholder="Name" maxLength={24} value={value} autoFocus onInput={(e) => setValue((e.target as HTMLInputElement).value)} />
      <button class="primary" type="submit" disabled={!value.trim()} data-testid="save-name">
        Save
      </button>
      <button type="button" onClick={onDone}>
        Cancel
      </button>
    </form>
  );
}

function Feed() {
  const feed = live.value.feed;
  const [naming, setNaming] = useState<number | null>(null);  // seq of the tap being named
  return (
    <Card title="Live taps" testId="feed">
      {feed.length === 0 ? (
        <p class="muted">No taps yet.</p>
      ) : (
        <ul class="feed">
          {feed.map((t) => (
            <li key={t.seq} class={`tone-${outcomeTone(t.outcome)}`} data-testid="feed-item">
              <span class="headline">{t.headline}</span>
              {t.player ? (
                <button class="who linklike" title="Name this rider" data-testid="name-rider" onClick={() => setNaming(naming === t.seq ? null : t.seq)}>
                  #{t.player} {t.name} ✎
                </button>
              ) : (
                <span class="who">{t.uid}</span>
              )}
              <span class="pts">
                {t.points > 0 ? `+${t.points}` : t.points !== 0 ? t.points : ''}
                {t.multiplier > 1 && t.points > 0 ? ` (${multiplierLabel(t.multiplier)})` : ''}
              </span>
              {naming === t.seq && <NameRider no={t.player} name={t.name} onDone={() => setNaming(null)} />}
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
        <div class="dev-player">{d.screen?.player || d.screen?.note}</div>
      </div>
      <p class="muted">
        LEDs: {d.leds ?? 'off'} · last sound: {d.sound ?? 'none'}
      </p>
    </Card>
  );
}

// Taps program cards instead of playing while this is on, so never let it go unnoticed.
function ProgrammingBanner() {
  const p = live.value.status?.provisioning;
  if (!p?.active) return null;
  return (
    <div class="programming-banner" role="alert" data-testid="programming-banner">
      <span>
        <strong>Programming cards</strong> (next #{p.next}). Taps don't score.
      </span>
      <button class="danger" onClick={() => act(() => api.stopProvisioning(), 'Done programming')}>
        Stop
      </button>
    </div>
  );
}

export function LivePage() {
  return (
    <div class="grid">
      <ProgrammingBanner />
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
