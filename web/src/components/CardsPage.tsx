import { useState } from 'preact/hooks';

import { randomUid } from '../format';
import { act, api, live } from '../store';
import type { ProvisionRecord, ProvisionResult } from '../types';
import { Card } from './common';

const RESULT: Record<ProvisionResult, { label: string; tone: string }> = {
  written: { label: 'Programmed', tone: 'success' },
  adopted: { label: 'Registered', tone: 'success' },
  already: { label: 'Already done', tone: 'warning' },
  failed: { label: 'Write failed', tone: 'error' },
  unsupported: { label: 'Wrong card type', tone: 'error' },
};

// Program a stack of numbered cards: card #n gets the text "orca-card-game-0n" and plays as
// rider #n. Only programmed cards play, and a card's number is who it is (a replacement card
// programmed with a lost card's number carries on with its score).
export function CardsPage() {
  const p = live.value.status?.provisioning;
  return (
    <div class="grid">
      {p?.active ? <Programming /> : <StartForm />}
      <ProgrammedLog records={live.value.programmed} />
    </div>
  );
}

function StartForm() {
  const [start, setStart] = useState('');
  const [overwrite, setOverwrite] = useState(false);
  const submit = (e: Event) => {
    e.preventDefault();
    act(() => api.startProvisioning({ ...(start ? { start: Number(start) } : {}), ...(overwrite ? { overwrite } : {}) }));
  };
  return (
    <Card title="Program cards">
      <form class="stack" onSubmit={submit}>
        <p class="muted">
          Put your printed cards in order, then tap them on the costume one by one. Each card gets the next number written
          onto it as <code>orca-card-game-01</code>, <code>-02</code>… Only programmed cards can play. Taps don't score while
          programming.
        </p>
        <label>
          Start at card number
          <input type="number" min="1" max="9999" placeholder="Next unused" value={start} onInput={(e) => setStart((e.target as HTMLInputElement).value)} />
        </label>
        <label class="check">
          <input type="checkbox" checked={overwrite} onChange={(e) => setOverwrite((e.target as HTMLInputElement).checked)} />
          Give cards that already have a number a new one
        </label>
        <button class="primary" type="submit" data-testid="provision-start">
          Start programming
        </button>
      </form>
    </Card>
  );
}

function Programming() {
  const p = live.value.status!.provisioning;
  return (
    <Card title="Programming cards" testId="provisioning">
      <p class="next-card">
        Tap card <strong data-testid="provision-next">#{p.next}</strong>
      </p>
      <p class="muted" data-testid="provision-count">
        {p.programmed === 1 ? '1 card' : `${p.programmed} cards`} programmed{p.overwrite ? ' · overwrite on' : ''}
      </p>
      <div class="row wrap">
        <button class="danger" data-testid="provision-stop" onClick={() => act(() => api.stopProvisioning(), 'Done programming')}>
          Done
        </button>
        <button data-testid="provision-sim" onClick={() => act(() => api.simTap({ uid: randomUid() }))}>
          Simulate a blank card
        </button>
      </div>
    </Card>
  );
}

export function ProgrammedLog({ records }: { records: ProvisionRecord[] }) {
  return (
    <Card title="This session">
      {records.length === 0 ? (
        <p class="muted">No cards programmed yet.</p>
      ) : (
        <ul class="feed" data-testid="programmed">
          {records.map((r, i) => (
            <li key={`${r.uid}-${records.length - i}`} class={`tone-${RESULT[r.result].tone}`}>
              <span class="headline">
                #{r.number} {RESULT[r.result].label}
              </span>
              <span class="who">
                {r.message} · {r.uid}
              </span>
            </li>
          ))}
        </ul>
      )}
    </Card>
  );
}
