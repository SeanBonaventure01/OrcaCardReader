// The other game modes from the phone. Runs after 1-operator (same orca-sim at 20x speed).
import { expect, type Page, test } from '@playwright/test';

test.describe.configure({ mode: 'serial' });

let page: Page;

test.beforeAll(async ({ browser }) => {
  page = await browser.newPage();
  await page.goto('/#/live');
  await expect(page.getByTestId('game-card')).toBeVisible();
});

test.afterAll(async () => {
  await page.close();
});

async function load(gameId: string, fields: Record<string, string> = {}) {
  await page.getByTestId('tab-live').click();
  const stop = page.getByTestId('action-stop');
  if (await stop.isVisible()) await stop.click();
  await page.getByTestId('tab-games').click();
  await page.getByTestId(`game-${gameId}`).click();
  const settings = page.getByTestId('game-settings');
  for (const [label, value] of Object.entries(fields)) await settings.getByLabel(label).fill(value);
  await page.getByTestId('load-start').click();
  await expect(page.getByTestId('game-state')).toHaveText('Running');
}

test('Last Stop shows who is on the bus', async () => {
  await load('last_stop', { 'Award points every minutes': '0', 'Award points every seconds': '10' });
  await page.getByLabel('Card to tap').selectOption('new');
  await page.getByTestId('sim-tap').click();
  await expect(page.getByTestId('feed-item').first()).toContainText('All aboard');
  await expect(page.getByTestId('game-status-line')).toContainText('On the bus:');
});

test('Lottery draws a winner from the Live page', async () => {
  await load('lottery', { 'Cooldown between taps minutes': '0', 'Cooldown between taps seconds': '0' });
  await page.getByLabel('Card to tap').selectOption('new');
  await page.getByTestId('sim-tap').click();
  await page.getByTestId('sim-tap').click();
  await page.getByTestId('game-action-draw').click();
  await expect(page.getByTestId('toast')).toContainText('Winner: #');
  await expect(page.getByTestId('announcement')).toContainText('Winner!');
  await expect(page.getByTestId('game-status-line')).toContainText('Winners: #');
});

test('Team Lines puts new riders on teams', async () => {
  await load('team_lines');
  await page.getByLabel('Card to tap').selectOption('new');
  await page.getByTestId('sim-tap').click();
  await expect(page.getByTestId('feed-item').first()).toContainText(/Good until/);
  await expect(page.getByTestId('teams')).toContainText('1 Line');
});
