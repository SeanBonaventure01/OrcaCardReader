import type { ComponentChildren } from 'preact';
import { useEffect, useState } from 'preact/hooks';

import { multiplierLabel } from '../format';
import type { Board } from '../types';

export function Card(props: { title?: string; children: ComponentChildren; testId?: string; actions?: ComponentChildren }) {
  return (
    <section class="card" data-testid={props.testId}>
      {(props.title || props.actions) && (
        <header class="card-head">
          {props.title && <h2>{props.title}</h2>}
          {props.actions}
        </header>
      )}
      {props.children}
    </section>
  );
}

// Re-renders every `ms` so countdowns move.
export function useNow(ms = 1000): number {
  const [now, setNow] = useState(Date.now());
  useEffect(() => {
    const id = setInterval(() => setNow(Date.now()), ms);
    return () => clearInterval(id);
  }, [ms]);
  return now;
}

export function Leaderboard({ board, limit, big }: { board: Board; limit?: number; big?: boolean }) {
  const rows = limit ? board.board.slice(0, limit) : board.board;
  if (rows.length === 0) return <p class="muted">No riders yet. Tap a card to get on the board.</p>;
  return (
    <div>
      <ol class={big ? 'board big' : 'board'} data-testid="leaderboard">
        {rows.map((r) => (
          <li key={r.no}>
            <span class="rank">{r.rank}</span>
            <span class="name">
              {r.name}
              {r.team && <span class="team"> · {r.team}</span>}
            </span>
            <span class="points">{r.points}</span>
          </li>
        ))}
      </ol>
      {board.teams.length > 0 && (
        <ol class="board teams" data-testid="teams">
          {board.teams.map((t, i) => (
            <li key={t.team}>
              <span class="rank">{i + 1}</span>
              <span class="name">{t.team}</span>
              <span class="points">{t.points}</span>
            </li>
          ))}
        </ol>
      )}
    </div>
  );
}

export function EventBadge({ name, multiplier, kind, remaining }: { name: string; multiplier: number; kind: string; remaining: string }) {
  return (
    <span class="event-badge" data-testid="event-badge">
      {name}
      {kind === 'multiplier' && ` ${multiplierLabel(multiplier)}`} · {remaining}
    </span>
  );
}
