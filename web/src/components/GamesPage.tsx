import { useEffect, useState } from 'preact/hooks';

import { type FormValues, fromFormValues, toFormValues } from '../paramForm';
import { act, api, live } from '../store';
import type { GameInfo } from '../types';
import { Card } from './common';
import { ParamFields } from './ParamFields';

export function GamesPage() {
  const [games, setGames] = useState<GameInfo[]>([]);
  const [selected, setSelected] = useState<GameInfo | null>(null);
  const [values, setValues] = useState<FormValues>({});
  const [errors, setErrors] = useState<Record<string, string>>({});
  const [keepScores, setKeepScores] = useState(false);
  const status = live.value.status;
  const busy = status?.state === 'running' || status?.state === 'paused';

  useEffect(() => {
    act(() => api.games()).then((g) => g && setGames(g));
  }, []);

  const pick = (g: GameInfo) => {
    setSelected(g);
    // Pre-fill with the current settings if this game is already loaded.
    const current = status?.game?.id === g.id ? status.game.params : {};
    setValues(toFormValues(g.params, current));
    setErrors({});
  };

  const load = async (start: boolean) => {
    if (!selected) return;
    const { params, errors: errs } = fromFormValues(selected.params, values);
    setErrors(errs);
    if (Object.keys(errs).length) return;
    const ok = await act(() => api.selectGame(selected.id, params, keepScores), start ? undefined : `${selected.name} loaded`);
    if (ok && start) await act(() => api.gameAction('start'), `${selected.name} started`);
    if (ok) location.hash = '#/live';
  };

  return (
    <div class="grid">
      {busy && (
        <Card>
          <p>
            <strong>{status?.game?.name}</strong> is {status?.state}. Stop it on the Live page before loading another game.
          </p>
        </Card>
      )}
      <Card title="Games" testId="games-list">
        <ul class="game-list">
          {games.map((g) => (
            <li key={g.id}>
              <button
                class={selected?.id === g.id ? 'game selected' : 'game'}
                data-testid={`game-${g.id}`}
                onClick={() => pick(g)}
              >
                <strong>{g.name}</strong>
                {status?.game?.id === g.id && <span class="chip">loaded</span>}
                <span class="muted">{g.description}</span>
              </button>
            </li>
          ))}
        </ul>
      </Card>
      {selected && (
        <Card title={`${selected.name} settings`} testId="game-settings">
          <ParamFields
            schema={selected.params}
            values={values}
            errors={errors}
            onChange={(k, v) => setValues({ ...values, [k]: v })}
          />
          <label class="check">
            <input type="checkbox" checked={keepScores} onChange={(e) => setKeepScores((e.target as HTMLInputElement).checked)} />
            Keep current scores
          </label>
          <div class="row">
            <button class="primary" disabled={busy} data-testid="load-start" onClick={() => load(true)}>
              Load &amp; start
            </button>
            <button disabled={busy} data-testid="load" onClick={() => load(false)}>
              Load only
            </button>
          </div>
        </Card>
      )}
    </div>
  );
}
