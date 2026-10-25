import { useEffect, useMemo, useState } from 'preact/hooks';

import { act, api, live } from '../store';
import type { Player } from '../types';
import { Card } from './common';

// Seattle transit lines make good team names.
const SUGGESTED_TEAMS = ['1 Line', '2 Line', 'Sounder', 'RapidRide', 'Water Taxi', 'Streetcar'];

function PlayerEditor({ player, teams, onDone }: { player: Player; teams: string[]; onDone: () => void }) {
  const [name, setName] = useState(player.name);
  const [team, setTeam] = useState(player.team);
  const save = async () => {
    const r = await act(() => api.updatePlayer(player.no, { name: name.trim(), team: team.trim() }), 'Saved');
    if (r) onDone();
  };
  return (
    <div class="editor" data-testid="player-editor">
      <label>
        Name
        <input aria-label="Name" value={name} maxLength={24} onInput={(e) => setName((e.target as HTMLInputElement).value)} />
      </label>
      <label>
        Team
        <input
          aria-label="Team"
          list="teams"
          value={team}
          maxLength={24}
          onInput={(e) => setTeam((e.target as HTMLInputElement).value)}
        />
        <datalist id="teams">
          {teams.map((t) => (
            <option key={t} value={t} />
          ))}
        </datalist>
      </label>
      <div class="row">
        <button onClick={() => act(() => api.adjustScore(player.no, -1)).then(onDone)}>−1 pt</button>
        <button onClick={() => act(() => api.adjustScore(player.no, 1)).then(onDone)}>+1 pt</button>
        <button onClick={() => act(() => api.adjustScore(player.no, 5)).then(onDone)}>+5 pts</button>
      </div>
      <div class="row">
        <button class="primary" data-testid="save-player" onClick={save} disabled={!name.trim()}>
          Save
        </button>
        <button
          class={player.banned ? '' : 'danger'}
          onClick={() => act(() => api.updatePlayer(player.no, { banned: !player.banned }), player.banned ? 'Reinstated' : 'Banned').then(onDone)}
        >
          {player.banned ? 'Unban' : 'Ban'}
        </button>
        <button onClick={onDone}>Close</button>
      </div>
    </div>
  );
}

// Before the party: clear out test riders so programmed cards start from #1. Two taps, so
// it can't happen by accident.
function RemoveAll({ count, onDone }: { count: number; onDone: () => void }) {
  const [armed, setArmed] = useState(false);
  const state = live.value.status?.state;
  const playing = state === 'running' || state === 'paused';
  useEffect(() => {
    if (!armed) return;
    const id = setTimeout(() => setArmed(false), 5000);
    return () => clearTimeout(id);
  }, [armed]);
  const remove = async () => {
    const r = await act(() => api.clearPlayers(), 'All riders removed');
    setArmed(false);
    if (r) onDone();
  };
  return (
    <Card title="Start fresh">
      <p class="muted">Removes every rider and score, e.g. test riders before the party. Cards stay programmed.</p>
      {armed ? (
        <div class="row">
          <button class="danger" data-testid="remove-all-confirm" onClick={remove}>
            Yes, remove {count === 1 ? '1 rider' : `${count} riders`}
          </button>
          <button onClick={() => setArmed(false)}>Cancel</button>
        </div>
      ) : (
        <button class="danger" data-testid="remove-all" disabled={playing || count === 0} onClick={() => setArmed(true)}>
          Remove all riders
        </button>
      )}
      {playing && <p class="muted">Stop the game first.</p>}
    </Card>
  );
}

export function PlayersPage() {
  const [players, setPlayers] = useState<Player[]>([]);
  const [query, setQuery] = useState('');
  const [editing, setEditing] = useState<number | null>(null);
  const [uid, setUid] = useState('');
  const [newName, setNewName] = useState('');
  const version = live.value.playersVersion;
  const boardVersion = live.value.board;

  const refresh = () => act(() => api.players()).then((p) => p && setPlayers(p));
  useEffect(() => {
    refresh();
  }, [version, boardVersion]);

  const teams = useMemo(
    () => [...new Set([...players.map((p) => p.team).filter(Boolean), ...SUGGESTED_TEAMS])],
    [players],
  );
  const shown = players.filter((p) => {
    const q = query.trim().toLowerCase();
    return !q || p.name.toLowerCase().includes(q) || String(p.no) === q || p.team.toLowerCase().includes(q);
  });

  const enroll = async (e: Event) => {
    e.preventDefault();
    const r = await act(() => api.enroll(uid.trim(), newName.trim() || undefined), 'Card added');
    if (r) {
      setUid('');
      setNewName('');
      refresh();
    }
  };

  return (
    <div class="grid">
      <Card title={`Riders (${players.length})`} testId="players">
        <input type="search" aria-label="Search riders" placeholder="Search name, number or team" value={query} onInput={(e) => setQuery((e.target as HTMLInputElement).value)} />
        <ul class="players">
          {shown.map((p) => (
            <li key={p.no} class={p.banned ? 'banned' : ''}>
              <button class="player-row" data-testid={`player-${p.no}`} onClick={() => setEditing(editing === p.no ? null : p.no)}>
                <span class="no">#{p.no}</span>
                <span class="name">
                  {p.name}
                  {p.team && <span class="team"> · {p.team}</span>}
                  {p.banned && <span class="chip state-ended">banned</span>}
                </span>
                <span class="points">{p.points}</span>
              </button>
              {editing === p.no && (
                <PlayerEditor
                  player={p}
                  teams={teams}
                  onDone={() => {
                    setEditing(null);
                    refresh();
                  }}
                />
              )}
            </li>
          ))}
        </ul>
      </Card>
      <RemoveAll count={players.length} onDone={refresh} />
      <Card title="Add a card by UID">
        <form class="stack" onSubmit={enroll}>
          <input aria-label="Card UID" placeholder="Card UID, e.g. 04A1B2C3D4E5F6" value={uid} onInput={(e) => setUid((e.target as HTMLInputElement).value)} />
          <input aria-label="Rider name" placeholder="Name (optional)" maxLength={24} value={newName} onInput={(e) => setNewName((e.target as HTMLInputElement).value)} />
          <button class="primary" type="submit" disabled={uid.trim().length < 8}>
            Add card
          </button>
        </form>
        <p class="muted">
          For cards you can't program. Programmed cards (see Cards) are added the first time they tap.
        </p>
      </Card>
    </div>
  );
}
