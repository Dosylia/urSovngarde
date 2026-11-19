import { createTest, expect } from '@ngx-playwright/test';

import { ApplicationScreen } from '../screens/main-screen.js';


const test = createTest(ApplicationScreen);

// The game tells the page where it is shown; the mock stands in for the game.
async function showOn(page, surface: string) {
  await page.evaluate(s => (globalThis as any).skyrimtogether.emit('setSurface', s), surface);
}

test.describe('Keyboard on the panel inside the game', () => {
  test('types into a pressed field on the panel only', async ({ page }) => {
    await page.waitForSelector('.app-root-controls', { state: 'attached' });
    await page.press('body', 'F2');
    await page.waitForSelector('.app-root-controls', { state: 'visible' });
    const input = page.locator('.app-chat-input input');
    const keyboard = page.locator('.app-keyboard');

    // The page focuses the chat by itself when it opens: that alone brings no keyboard up.
    await showOn(page, 'panel');
    await page.evaluate(() => (document.querySelector('.app-chat-input input') as HTMLInputElement).focus());
    await expect(keyboard).toHaveCount(0);

    // A flat screen has a real keyboard: none on the page, even for a pressed field.
    await showOn(page, 'screen');
    await input.click();
    await expect(keyboard).toHaveCount(0);

    // On the panel inside the game a pressed field gets the page's own.
    await showOn(page, 'panel');
    await input.click();
    await expect(keyboard).toBeVisible();
    for (const key of ['r', 'g', 'k', '-']) {
      await page.click(`.app-keyboard-key[data-key="${key}"]`);
    }
    await expect(input).toHaveValue('rgk-');
    await expect(input).toBeFocused();
    await expect(page.locator('.app-keyboard-echo')).toHaveText('rgk-');

    await page.click('.app-keyboard-key[data-key="backspace"]');
    await expect(input).toHaveValue('rgk');

    // Shift makes one capital; twice quickly, all of them.
    await page.click('.app-keyboard-key[data-key="shift"]');
    await page.click('.app-keyboard-key[data-key="j"]');
    await page.click('.app-keyboard-key[data-key="m"]');
    await expect(input).toHaveValue('rgkJm');

    // Enter is the real key's: the chat sends the line (watched at the game's side, as the mock sends only when connected).
    await page.evaluate(() => {
      const game = (globalThis as any).skyrimtogether;
      game.__sent = [];
      game.sendMessage = (type, content) => game.__sent.push(content);
    });
    await page.click('.app-keyboard-key[data-key="enter"]');
    await expect.poll(() => page.evaluate(() => (globalThis as any).skyrimtogether.__sent)).toEqual(['rgkJm']);
    await expect(input).toHaveValue('');

    // Closing it lets the field go.
    await page.click('.app-keyboard-key[data-key="close"]');
    await expect(keyboard).toHaveCount(0);
    await expect(input).not.toBeFocused();
  });
});
