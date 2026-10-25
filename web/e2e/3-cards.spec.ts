// Programming a stack of key cards from the phone. Runs after 2-games (same orca-sim), so
// some riders already exist and their numbers are skipped.
import { expect, type Page, test } from '@playwright/test';

test.describe.configure({ mode: 'serial' });

let page: Page;

test.beforeAll(async ({ browser }) => {
  page = await browser.newPage();
  await page.goto('/#/cards');
  await expect(page.getByTestId('provision-start')).toBeVisible();
});

test.afterAll(async () => {
  await page.close();
});

test('the Cards tab fits in the tab bar on a phone', async () => {
  const tab = page.getByTestId('tab-cards');
  await expect(tab).toHaveText('Cards');
  const box = (await tab.boundingBox())!;
  expect(box.x + box.width).toBeLessThanOrEqual(page.viewportSize()!.width);
  await expect(tab).toHaveClass(/active/);
});

test('start programming at 100 and program three cards', async () => {
  await page.getByLabel('Start at card number').fill('100');
  await page.getByTestId('provision-start').click();
  await expect(page.getByTestId('provision-next')).toHaveText('#100');
  for (let n = 100; n < 103; n++) {
    await page.getByTestId('provision-sim').click();
    await expect(page.getByTestId('provision-next')).toHaveText(`#${n + 1}`);
  }
  await expect(page.getByTestId('provision-count')).toHaveText('3 cards programmed');
  const log = page.getByTestId('programmed').locator('li');
  await expect(log).toHaveCount(3);
  await expect(log.first()).toContainText('#102 Programmed');
});

test('done returns to the start form, and the new riders exist', async () => {
  await page.getByTestId('provision-stop').click();
  await expect(page.getByTestId('provision-start')).toBeVisible();
  await page.getByTestId('tab-players').click();
  await expect(page.getByText('#102').first()).toBeVisible();
});

test('name a rider straight from the live feed', async () => {
  await page.getByTestId('tab-live').click();
  await page.getByLabel('Card to tap').selectOption({ label: '#100 Rider 100' });
  await page.getByTestId('sim-tap').click();
  const item = page.getByTestId('feed-item').first();
  await expect(item).toContainText('#100 Rider 100');
  await item.getByTestId('name-rider').click();
  await page.getByLabel('Name for rider 100').fill('Morticia');
  await page.getByTestId('save-name').click();
  await expect(page.getByTestId('toast')).toHaveText('#100 is now Morticia');
  await page.getByTestId('tab-players').click();
  await expect(page.getByTestId('player-100')).toContainText('Morticia');
});

test('remove all riders needs the game stopped and a second tap', async () => {
  await expect(page.getByTestId('remove-all')).toBeDisabled();  // a game is still running
  await page.getByTestId('tab-live').click();
  await page.getByTestId('action-stop').click();
  await page.getByTestId('tab-players').click();
  await page.getByTestId('remove-all').click();
  await page.getByTestId('remove-all-confirm').click();
  await expect(page.getByTestId('players')).toContainText('Riders (0)');
});

test('profanity voice and effects are settings that stick', async () => {
  await page.getByTestId('tab-settings').click();
  await expect(page.getByTestId('voice')).toHaveValue('off');
  await expect(page.getByTestId('effects')).toBeChecked();
  await page.getByTestId('voice').selectOption('profanity');
  await page.getByTestId('effects').uncheck();
  await page.reload();
  await page.getByTestId('tab-settings').click();
  await expect(page.getByTestId('voice')).toHaveValue('profanity');
  await expect(page.getByTestId('effects')).not.toBeChecked();
});

test('programming shows on the Live tab, and starting a game ends it', async () => {
  await page.getByTestId('tab-cards').click();
  await page.getByTestId('provision-start').click();
  await page.getByTestId('tab-live').click();
  await expect(page.getByTestId('programming-banner')).toContainText('Programming cards');
  await page.getByTestId('tab-games').click();
  await page.getByTestId('game-fare_paid').click();
  await page.getByTestId('load-start').click();
  await page.getByTestId('tab-live').click();
  await expect(page.getByTestId('game-state')).toHaveText('Running');
  await expect(page.getByTestId('programming-banner')).toHaveCount(0);
  await page.getByTestId('action-stop').click();  // leave no game running for the next spec
  await expect(page.getByTestId('game-state')).toHaveText('Ended');
});
