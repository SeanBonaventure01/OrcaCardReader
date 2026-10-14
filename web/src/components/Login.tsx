import { useState } from 'preact/hooks';

import { ApiError } from '../api';
import { api, loggedIn } from '../store';

export function Login() {
  const [pin, setPin] = useState('');
  const [error, setError] = useState('');
  const [busy, setBusy] = useState(false);

  const submit = async (e: Event) => {
    e.preventDefault();
    setBusy(true);
    setError('');
    try {
      await api.login(pin);
      loggedIn.value = true;
      api.syncClock().catch(() => {});
    } catch (err) {
      setError(err instanceof ApiError ? err.message : 'Cannot reach the costume');
      setPin('');
    } finally {
      setBusy(false);
    }
  };

  return (
    <form class="login card" onSubmit={submit} data-testid="login">
      <h2>Operator login</h2>
      <p class="muted">Enter the admin PIN from config.json.</p>
      <input
        type="password"
        inputMode="numeric"
        autoComplete="current-password"
        aria-label="PIN"
        placeholder="PIN"
        value={pin}
        onInput={(e) => setPin((e.target as HTMLInputElement).value)}
        autoFocus
      />
      {error && (
        <p class="error" role="alert">
          {error}
        </p>
      )}
      <button class="primary" type="submit" disabled={busy || pin.length < 4}>
        Log in
      </button>
      <a href="#/board">View the public leaderboard</a>
    </form>
  );
}
