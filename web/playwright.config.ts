import { defineConfig, devices } from '@playwright/test';

// E2E: the built phone UI against orca-sim (the real C++ engine + API) at 20x speed.
// Requires the host build: `make host-test` (or `make web-e2e`, which does everything).
const PORT = 8090;

export default defineConfig({
  testDir: 'e2e',
  fullyParallel: false,
  workers: 1,
  retries: 0,
  reporter: [['list']],
  use: {
    ...devices['Pixel 7'],
    baseURL: `http://localhost:${PORT}`,
    trace: 'retain-on-failure',
  },
  webServer: {
    command: `npm run build --silent && ../host/build/orca-sim serve --port ${PORT} --speed 20 --web dist --quiet --seed 3`,
    url: `http://localhost:${PORT}/api/health`,
    reuseExistingServer: false,
    timeout: 60_000,
  },
});
