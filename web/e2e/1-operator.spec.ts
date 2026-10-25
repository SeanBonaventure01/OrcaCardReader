// Runs first (specs share one orca-sim and run in file-name order).
// A night at the party, as the operator sees it on their phone. Steps share one simulated
// costume (orca-sim at 20x speed), so they run in order.
import { expect, type Page, test } from '@playwright/test';

test.describe.configure({ mode: 'serial' });

let page: Page;

test.beforeAll(async ({ browser }) => {
  page = await browser.newPage();
});

test.afterAll(async () => {
  await page.close();
});

async function tab(name: 'live' | 'games' | 'players' | 'events' | 'settings') {
  await page.getByTestId(`tab-${name}`).click();
}

test('public leaderboard works without logging in', async () => {
  await page.goto('/#/board');
  await expect(page.getByTestId('public-board')).toContainText('ORCA Leaderboard');
  await expect(page.getByText('No riders yet')).toBeVisible();
});

test('opens straight to the controls (no PIN)', async () => {
  await page.goto('/#/live');
  await expect(page.getByTestId('game-name')).toHaveText('No game selected');
  await expect(page.getByTestId('conn')).toHaveClass(/on/);
  await expect(page.getByLabel('PIN')).toHaveCount(0);
});

test('load and start Fare Paid with a custom cooldown', async () => {
  await tab('games');
  await page.getByTestId('game-fare_paid').click();
  const settings = page.getByTestId('game-settings');
  await expect(settings.getByLabel('Cooldown between taps minutes')).toHaveValue('5');
  // Invalid value is caught before sending.
  await settings.getByLabel('Points per fare').fill('0');
  await page.getByTestId('load-start').click();
  await expect(settings.getByRole('alert')).toContainText('between 1 and 100');
  await settings.getByLabel('Points per fare').fill('1');
  await settings.getByLabel('Cooldown between taps minutes').fill('0');
  await settings.getByLabel('Cooldown between taps seconds').fill('20');
  await page.getByTestId('load-start').click();
  await expect(page.getByTestId('game-state')).toHaveText('Running');
  await expect(page.getByTestId('game-name')).toHaveText('Fare Paid');
});

test('a simulated tap shows in the feed, leaderboard and costume preview', async () => {
  await page.getByTestId('sim-tap').click();
  const first = page.getByTestId('feed-item').first();
  await expect(first).toContainText(/Good until \d+:\d\d [ap]m/);  // clock synced on connect
  await expect(first).toContainText('#1 Rider 1');
  await expect(page.getByTestId('leaderboard')).toContainText('Rider 1');
  await expect(page.getByTestId('device-screen')).toContainText(/Good until/);
  await expect(page.getByTestId('game-card')).toContainText('1 riders');
});

test('tapping again during the cooldown is a transfer', async () => {
  await page.getByLabel('Card to tap').selectOption({ label: '#1 Rider 1' });
  await page.getByTestId('sim-tap').click();
  await expect(page.getByTestId('feed-item').first()).toContainText('Transfer');
  await expect(page.getByTestId('device-screen')).toContainText('Next fare in');
});

test('rush hour doubles points', async () => {
  await page.getByTestId('quick-rush').click();
  await expect(page.getByTestId('event-badge').first()).toContainText('Rush hour 2x');
  await expect(page.getByTestId('announcement')).toContainText('Rush hour');
  // 20 s game-time cooldown = 1 s real time at 20x.
  await page.waitForTimeout(1500);
  await page.getByTestId('sim-tap').click();
  const first = page.getByTestId('feed-item').first();
  await expect(first).toContainText(/Good until/);
  await expect(first).toContainText('+2 (2x)');
});

test('rename a rider and put them on a team', async () => {
  await tab('players');
  await page.getByTestId('player-1').click();
  const editor = page.getByTestId('player-editor');
  await editor.getByLabel('Name').fill('Ghost');
  await editor.getByLabel('Team').fill('Sounder');
  await page.getByTestId('save-player').click();
  await expect(page.getByTestId('player-1')).toContainText('Ghost');
  await expect(page.getByTestId('player-1')).toContainText('Sounder');
});

test('the public board updates live in another tab', async ({ browser }) => {
  const guest = await browser.newPage();
  await guest.goto('/#/board');
  await expect(guest.getByTestId('leaderboard')).toContainText('Ghost');
  await expect(guest.getByTestId('teams')).toContainText('Sounder');
  // A new rider tapping shows up for the guest without a reload.
  await tab('live');
  await page.getByLabel('Card to tap').selectOption('new');
  await page.getByTestId('sim-tap').click();
  await expect(guest.getByTestId('leaderboard')).toContainText('Rider 2');
  await guest.close();
});

test('closing enrollment rejects unknown cards', async () => {
  await tab('settings');
  await page.getByTestId('open-enrollment').uncheck();
  await tab('live');
  await page.getByLabel('Card to tap').selectOption('new');
  await page.getByTestId('sim-tap').click();
  await expect(page.getByTestId('feed-item').first()).toContainText('Card not recognized');
  await tab('settings');
  await page.getByTestId('open-enrollment').check();
});

test('custom event from the Events page', async () => {
  await tab('events');
  await page.getByRole('combobox').first().selectOption('cooldown_waiver');
  await page.getByLabel('Name shown on the costume').fill('Free ride');
  await page.getByTestId('trigger-event').click();
  await expect(page.getByTestId('active-events')).toContainText('Free ride');
});

test('stop ends the game and announces the winner', async () => {
  await tab('live');
  await page.getByTestId('action-stop').click();
  await expect(page.getByTestId('game-state')).toHaveText('Ended');
  await expect(page.getByTestId('announcement')).toContainText('End of the line');
  await expect(page.getByTestId('announcement')).toContainText('Winner: Ghost');
  await expect(page.getByTestId('action-reset')).toBeVisible();
});
