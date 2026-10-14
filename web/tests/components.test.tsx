import { cleanup, fireEvent, render, screen } from '@testing-library/preact';
import { afterEach, describe, expect, it } from 'vitest';

import { Leaderboard } from '../src/components/common';
import { ParamFields } from '../src/components/ParamFields';
import { toFormValues } from '../src/paramForm';
import type { Board, GameInfo } from '../src/types';
import { loadFixtures, materialize } from './fixtures';

const fixtures = loadFixtures();
const games = (materialize(fixtures.get('games_list')!.response.body) as { games: GameInfo[] }).games;

afterEach(cleanup);

describe('Leaderboard', () => {
  it('renders players and teams from the board fixture', () => {
    const board = materialize(fixtures.get('board')!.response.body) as Board;
    render(<Leaderboard board={board} />);
    const rows = screen.getByTestId('leaderboard').querySelectorAll('li');
    expect(rows).toHaveLength(2);
    expect(rows[0].textContent).toContain('Witch');
    expect(rows[0].textContent).toContain('Sounder');
    expect(screen.getByTestId('teams').textContent).toContain('Sounder');
  });

  it('shows a hint when empty', () => {
    render(<Leaderboard board={{ board: [], teams: [] }} />);
    expect(screen.getByText(/No riders yet/)).toBeTruthy();
  });

  it('respects the limit', () => {
    const board: Board = {
      board: Array.from({ length: 20 }, (_, i) => ({ rank: i + 1, no: i + 1, name: `R${i}`, team: '', points: 20 - i })),
      teams: [],
    };
    render(<Leaderboard board={board} limit={10} />);
    expect(screen.getByTestId('leaderboard').querySelectorAll('li')).toHaveLength(10);
  });
});

describe('ParamFields', () => {
  it('renders one field per schema entry, durations as min + s', () => {
    const rush = games.find((g) => g.id === 'rush_hour')!;
    const values = toFormValues(rush.params);
    render(<ParamFields schema={rush.params} values={values} errors={{}} onChange={() => {}} />);
    expect(screen.getByLabelText('Cooldown between taps minutes')).toBeTruthy();
    expect(screen.getByLabelText('Cooldown between taps seconds')).toBeTruthy();
    expect((screen.getByLabelText('Points per fare') as HTMLInputElement).value).toBe('1');
    expect((screen.getByLabelText('Points multiplier') as HTMLInputElement).step).toBe('0.1');
  });

  it('reports edits with the right keys and shows errors', () => {
    const fare = games.find((g) => g.id === 'fare_paid')!;
    const changes: [string, unknown][] = [];
    render(
      <ParamFields
        schema={fare.params}
        values={toFormValues(fare.params)}
        errors={{ points: 'Must be between 1 and 100' }}
        onChange={(k, v) => changes.push([k, v])}
      />,
    );
    fireEvent.input(screen.getByLabelText('Cooldown between taps minutes'), { target: { value: '2' } });
    fireEvent.input(screen.getByLabelText('Points per fare'), { target: { value: '7' } });
    expect(changes).toEqual([
      ['cooldown.min', '2'],
      ['points', '7'],
    ]);
    expect(screen.getByRole('alert').textContent).toBe('Must be between 1 and 100');
  });
});
