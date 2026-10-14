import { mmss, stateLabel } from '../format';
import { engineNow } from '../live';
import { live } from '../store';
import { EventBadge, Leaderboard, useNow } from './common';

// Public leaderboard for guests (no login).
export function BoardPage() {
  const now = useNow();
  const s = live.value.status;
  return (
    <div class="public-board" data-testid="public-board">
      <h1>ORCA Leaderboard</h1>
      {s && (
        <p class="muted">
          {s.game?.name ?? 'No game'} · {stateLabel[s.state]}
        </p>
      )}
      {s?.events.map((e) => (
        <EventBadge key={e.id} name={e.name} kind={e.kind} multiplier={e.multiplier} remaining={mmss(e.end - engineNow(live.value, now))} />
      ))}
      <Leaderboard board={live.value.board} big />
    </div>
  );
}
