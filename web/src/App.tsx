import { useEffect, useState } from 'preact/hooks';

import { BoardPage } from './components/BoardPage';
import { CardsPage } from './components/CardsPage';
import { EventsPage } from './components/EventsPage';
import { GamesPage } from './components/GamesPage';
import { LivePage } from './components/LivePage';
import { PlayersPage } from './components/PlayersPage';
import { SettingsPage } from './components/SettingsPage';
import { stateLabel } from './format';
import { connected, live, toast } from './store';

const TABS = [
  { route: 'live', label: 'Live' },
  { route: 'games', label: 'Games' },
  { route: 'players', label: 'Riders' },
  { route: 'cards', label: 'Cards' },
  { route: 'events', label: 'Events' },
  { route: 'settings', label: 'Settings' },
] as const;

function useRoute(): string {
  const read = () => location.hash.replace(/^#\/?/, '') || 'live';
  const [route, setRoute] = useState(read);
  useEffect(() => {
    const on = () => setRoute(read());
    addEventListener('hashchange', on);
    return () => removeEventListener('hashchange', on);
  }, []);
  return route;
}

function Announcement() {
  const a = live.value.announcement;
  const [hidden, setHidden] = useState<number | null>(null);
  useEffect(() => {
    if (!a) return;
    const id = setTimeout(() => setHidden(a.at), 4000);
    return () => clearTimeout(id);
  }, [a?.at]);
  if (!a || hidden === a.at) return null;
  return (
    <div class="announcement" role="status" data-testid="announcement">
      <strong>{a.headline}</strong> {a.detail}
    </div>
  );
}

export function App() {
  const route = useRoute();
  const s = live.value.status;

  if (route === 'board') return <BoardPage />;

  return (
    <div class="app">
      <header class="top">
        <span class={connected.value ? 'dot on' : 'dot'} title={connected.value ? 'Connected' : 'Disconnected'} data-testid="conn" />
        <h1>ORCA Reader</h1>
        {s && <span class={`chip state-${s.state}`}>{stateLabel[s.state]}</span>}
      </header>
      <Announcement />
      <main>
        {route === 'games' && <GamesPage />}
        {route === 'players' && <PlayersPage />}
        {route === 'cards' && <CardsPage />}
        {route === 'events' && <EventsPage />}
        {route === 'settings' && <SettingsPage />}
        {!['games', 'players', 'cards', 'events', 'settings'].includes(route) && <LivePage />}
      </main>
      <nav class="tabs">
        {TABS.map((t) => (
          <a key={t.route} href={`#/${t.route}`} class={route === t.route ? 'active' : ''} data-testid={`tab-${t.route}`}>
            {t.label}
          </a>
        ))}
      </nav>
      {toast.value && (
        <div class={toast.value.error ? 'toast error' : 'toast'} role="status" data-testid="toast">
          {toast.value.text}
        </div>
      )}
    </div>
  );
}
