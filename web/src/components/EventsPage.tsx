import { useState } from 'preact/hooks';

import { mmss } from '../format';
import { engineNow } from '../live';
import { act, api, live } from '../store';
import type { EventKind } from '../types';
import { Card, EventBadge, useNow } from './common';

const SOUNDS = ['accept', 'reject', 'bonus', 'start', 'end', 'event_start', 'event_end'];

export function EventsPage() {
  const [kind, setKind] = useState<EventKind>('multiplier');
  const [name, setName] = useState('RUSH HOUR');
  const [minutes, setMinutes] = useState('2');
  const [seconds, setSeconds] = useState('0');
  const [mult, setMult] = useState('2');
  const now = useNow();
  const s = live.value.status;
  const running = s?.state === 'running';

  const trigger = (e: Event) => {
    e.preventDefault();
    const duration_ms = (Number(minutes || 0) * 60 + Number(seconds || 0)) * 1000;
    act(
      () => api.triggerEvent({ kind, name: name.trim(), duration_ms, ...(kind === 'multiplier' ? { multiplier: Number(mult) } : {}) }),
      `${name || 'Event'} started`,
    );
  };

  return (
    <div class="grid">
      <Card title="Active events" testId="active-events">
        {s?.events.length ? (
          <div class="events">
            {s.events.map((e) => (
              <EventBadge key={e.id} name={e.name} kind={e.kind} multiplier={e.multiplier} remaining={mmss(e.end - engineNow(live.value, now))} />
            ))}
          </div>
        ) : (
          <p class="muted">None right now.</p>
        )}
        <button class="danger" disabled={!s?.events.length} onClick={() => act(() => api.endEvents(), 'Events ended')}>
          End all events
        </button>
      </Card>
      <Card title="Start an event">
        <form class="stack" onSubmit={trigger}>
          <label>
            Type
            <select value={kind} onChange={(e) => setKind((e.target as HTMLSelectElement).value as EventKind)}>
              <option value="multiplier">Points multiplier</option>
              <option value="cooldown_waiver">Free ride (no cooldowns)</option>
            </select>
          </label>
          <label>
            Name shown on the costume
            <input value={name} maxLength={24} onInput={(e) => setName((e.target as HTMLInputElement).value)} />
          </label>
          <div class="duration">
            <input type="number" min="0" aria-label="Minutes" value={minutes} onInput={(e) => setMinutes((e.target as HTMLInputElement).value)} />
            <span>min</span>
            <input type="number" min="0" max="59" aria-label="Seconds" value={seconds} onInput={(e) => setSeconds((e.target as HTMLInputElement).value)} />
            <span>s</span>
          </div>
          {kind === 'multiplier' && (
            <label>
              Multiplier
              <input type="number" step="0.5" min="1" max="10" value={mult} onInput={(e) => setMult((e.target as HTMLInputElement).value)} />
            </label>
          )}
          <button class="primary" type="submit" disabled={!running} data-testid="trigger-event">
            Start event
          </button>
          {!running && <p class="muted">A game must be running.</p>}
        </form>
      </Card>
      <Card title="Soundboard">
        <div class="row wrap">
          {SOUNDS.map((id) => (
            <button key={id} onClick={() => act(() => api.playSound(id))}>
              {id.replace('_', ' ')}
            </button>
          ))}
        </div>
      </Card>
    </div>
  );
}
