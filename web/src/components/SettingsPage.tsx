import { useEffect, useState } from 'preact/hooks';

import { act, api, live, loggedIn } from '../store';
import type { Settings } from '../types';
import { Card } from './common';

const SLIDERS: { key: keyof Settings; label: string; min: number; max: number }[] = [
  { key: 'volume', label: 'Volume', min: 0, max: 100 },
  { key: 'brightness', label: 'Screen brightness', min: 5, max: 100 },
  { key: 'led_brightness', label: 'LED brightness', min: 0, max: 100 },
];

export function SettingsPage() {
  const [settings, setSettings] = useState<Settings | null>(null);
  const open = live.value.status?.open_enrollment ?? true;

  useEffect(() => {
    act(() => api.settings()).then((s) => s && setSettings(s));
  }, []);

  const patch = async (p: Partial<Settings>) => {
    const r = await act(() => api.patchSettings(p));
    if (r) setSettings(r);
  };

  return (
    <div class="grid">
      <Card title="Costume" testId="settings">
        {settings ? (
          <div class="stack">
            {SLIDERS.map((s) => (
              <label key={s.key}>
                {s.label}: <b>{settings[s.key]}%</b>
                <input
                  type="range"
                  min={s.min}
                  max={s.max}
                  value={settings[s.key]}
                  aria-label={s.label}
                  onChange={(e) => patch({ [s.key]: Number((e.target as HTMLInputElement).value) })}
                />
              </label>
            ))}
            <label>
              LED count
              <input type="number" min="0" max="300" value={settings.led_count} onChange={(e) => patch({ led_count: Number((e.target as HTMLInputElement).value) })} />
            </label>
            <label>
              LED current limit (mA)
              <input type="number" min="50" max="2000" step="50" value={settings.led_max_ma} onChange={(e) => patch({ led_max_ma: Number((e.target as HTMLInputElement).value) })} />
            </label>
          </div>
        ) : (
          <p class="muted">Loading…</p>
        )}
      </Card>
      <Card title="Riders">
        <label class="check">
          <input
            type="checkbox"
            checked={open}
            data-testid="open-enrollment"
            onChange={(e) => act(() => api.setEnrollment((e.target as HTMLInputElement).checked))}
          />
          Open enrollment (unknown cards join automatically)
        </label>
      </Card>
      <Card title="Session">
        <div class="row">
          <button onClick={() => act(() => api.syncClock(), 'Clock synced')}>Sync clock from phone</button>
          <button
            class="danger"
            onClick={async () => {
              await api.logout().catch(() => {});
              loggedIn.value = false;
            }}
          >
            Log out
          </button>
        </div>
      </Card>
    </div>
  );
}
