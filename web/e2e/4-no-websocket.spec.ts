// The Live tab must work even if the costume's WebSocket doesn't (a flaky phone connection,
// a stuck handshake): status comes over plain HTTP polling instead. Runs last.
import { expect, test } from '@playwright/test';

test('without a WebSocket the Live tab still shows and stops the game', async ({ page }) => {
  await page.routeWebSocket('**/ws', (ws) => ws.close());  // every connection fails
  await page.goto('/#/live');
  await expect(page.getByTestId('conn')).not.toHaveClass(/on/);
  await expect(page.getByTestId('game-name')).toBeVisible({ timeout: 8000 });
  await page.getByTestId('tab-games').click();
  await page.getByTestId('game-fare_paid').click();
  await page.getByTestId('load-start').click();
  await page.getByTestId('tab-live').click();
  await expect(page.getByTestId('game-state')).toHaveText('Running', { timeout: 8000 });
  await page.getByTestId('action-stop').click();
  await expect(page.getByTestId('game-state')).toHaveText('Ended', { timeout: 8000 });
});
