import { test, expect } from '@playwright/test';
import { readFileSync } from 'node:fs';

const snapshot = JSON.parse(readFileSync(new URL('../fixtures/lageos1-trajectory-v1.json', import.meta.url), 'utf8'));
const selectedSnapshots = ['lageos1', 'lageos2', 'lares', 'lares2'].map(key => JSON.parse(readFileSync(new URL(`../fixtures/${key}-trajectory-v1.json`, import.meta.url), 'utf8')));

test('renders offline and responds to the native camera and grid commands', async ({ page }) => {
  test.setTimeout(60_000);
  const errors: string[] = [];
  const externalRequests: string[] = [];
  page.on('pageerror', error => errors.push(error.message));
  page.on('request', request => {
    if (!request.url().startsWith('http://127.0.0.1:4173') && !/^(blob:|data:)/.test(request.url())) externalRequests.push(request.url());
  });
  await page.addInitScript(() => {
    const bridge = { messages: [] as unknown[], receive: (_event: MessageEvent) => {} };
    Object.assign(window, { testBridge: bridge, chrome: { webview: {
      postMessage: (message: unknown) => bridge.messages.push(message),
      addEventListener: (_type: string, listener: (event: MessageEvent) => void) => { bridge.receive = listener; },
    } } });
  });
  await page.goto('http://127.0.0.1:4173');
  await page.waitForFunction(() => (window as any).testBridge.messages.some((message: any) => message.type === 'ready'));
  await expect(page.locator('#error')).toBeHidden();
  await expect(page.locator('canvas')).toBeVisible();
  // Wait for the bundled imagery tiles to finish drawing before comparing views.
  await page.waitForLoadState('networkidle');
  await page.waitForTimeout(500);
  const global = await page.screenshot({ path: 'test-results/globe-global.png' });
  await page.evaluate(() => (window as any).testBridge.receive({ data: { command: 'observer' } }));
  await page.waitForTimeout(2200);
  const observer = await page.screenshot({ path: 'test-results/globe-observer.png' });
  expect(observer.equals(global)).toBe(false);
  await page.evaluate(() => (window as any).testBridge.receive({ data: { command: 'grid-on' } }));
  await page.waitForTimeout(500);
  const grid = await page.screenshot({ path: 'test-results/globe-grid.png' });
  expect(grid.equals(observer)).toBe(false);
  await page.evaluate(() => (window as any).testBridge.receive({ data: { command: 'grid-off' } }));
  await page.waitForTimeout(500);
  expect((await page.screenshot()).equals(grid)).toBe(false);
  await page.evaluate(snapshot => (window as any).testBridge.receive({ data: { command: 'load-trajectory', snapshot } }), snapshot);
  await page.waitForFunction(() => (window as any).testBridge.messages.some((message: any) => message.type === 'trajectory-loaded' && message.sampleCount === 481));
  await page.evaluate(() => (window as any).testBridge.receive({ data: { command: 'trajectory-view' } }));
  await page.waitForTimeout(2000);
  await page.waitForLoadState('networkidle');
  await expect(page.locator('#trajectory-info')).toContainText('LAGEOS-1');
  const trajectory = await page.screenshot({ path: 'test-results/lageos1-trajectory.png' });
  await page.evaluate(() => (window as any).testBridge.receive({ data: { command: 'reference-off' } }));
  await page.waitForTimeout(500);
  const hidden = await page.screenshot();
  expect(hidden.equals(trajectory)).toBe(false);
  await page.evaluate(() => (window as any).testBridge.receive({ data: { command: 'reference-on' } }));
  await page.waitForTimeout(500);
  expect((await page.screenshot()).equals(hidden)).toBe(false);
  await page.evaluate(() => (window as any).testBridge.receive({ data: { command: 'trajectory-on' } }));
  await page.waitForTimeout(500);
  expect((await page.screenshot()).equals(trajectory)).toBe(false);
  await page.evaluate(() => (window as any).testBridge.receive({ data: { command: 'trajectory-off' } }));
  // Invalid replacements remove the old path and fail closed.
  await page.evaluate(snapshot => (window as any).testBridge.receive({ data: { command: 'load-trajectory', snapshot: { ...snapshot, schemaVersion: 99 } } }), snapshot);
  await page.waitForFunction(() => (window as any).testBridge.messages.some((message: any) => message.type === 'trajectory-error'));
  await expect(page.locator('#trajectory-info')).toBeHidden();
  await page.evaluate(snapshot => (window as any).testBridge.receive({ data: { command: 'load-trajectory', snapshot } }), snapshot);
  await expect(page.locator('#trajectory-info')).toBeVisible();
  await page.evaluate(() => (window as any).testBridge.receive({ data: { command: 'clear-trajectory' } }));
  await expect(page.locator('#trajectory-info')).toBeHidden();
  await page.evaluate(snapshots => (window as any).testBridge.receive({ data: { command: 'set-trajectories', snapshots, requestId: 'four-targets' } }), selectedSnapshots);
  await page.waitForFunction(() => (window as any).testBridge.messages.some((message: any) => message.type === 'trajectory-loaded' && message.requestId === 'four-targets' && message.keys.length === 4));
  await page.evaluate(() => (window as any).testBridge.receive({ data: { command: 'trajectory-view' } }));
  await page.waitForTimeout(2000);
  await expect(page.locator('#trajectory-info')).toContainText('4 selected satellites');
  await page.screenshot({ path: 'test-results/reference-orbits.png' });
  const perth = { name: 'Perth', latitudeDeg: -31.95, longitudeDeg: 115.86, ellipsoidalHeightM: 35, planeWidthKm: 12000, showPlane: true, minElevationDeg: 12.5, showElevationCone: true };
  await page.evaluate(observer => (window as any).testBridge.receive({ data: { command: 'set-observer', observer } }), perth);
  await page.waitForFunction(() => (window as any).testBridge.messages.some((message: any) => message.type === 'observer-updated' && message.observer.name === 'Perth'));
  await page.evaluate(() => (window as any).testBridge.receive({ data: { command: 'observer-side' } }));
  await page.waitForLoadState('networkidle');
  await page.waitForTimeout(500);
  const planeSide = await page.screenshot({ path: 'test-results/tangent-plane-side.png' });
  await page.evaluate(observer => (window as any).testBridge.receive({ data: { command: 'set-observer', observer: { ...observer, showElevationCone: false } } }), perth);
  await page.waitForTimeout(500);
  expect((await page.screenshot()).equals(planeSide)).toBe(false);
  await page.evaluate(observer => (window as any).testBridge.receive({ data: { command: 'set-observer', observer } }), perth);
  await page.waitForTimeout(500);
  await page.evaluate(observer => (window as any).testBridge.receive({ data: { command: 'set-observer', observer: { ...observer, showPlane: false } } }), perth);
  await page.waitForTimeout(500);
  expect((await page.screenshot()).equals(planeSide)).toBe(false);
  await page.evaluate(observer => (window as any).testBridge.receive({ data: { command: 'set-observer', observer } }), perth);
  await page.waitForTimeout(500);
  const restoredPlane = await page.screenshot();
  await page.evaluate(observer => (window as any).testBridge.receive({ data: { command: 'set-observer', observer: { ...observer, latitudeDeg: 91 } } }), perth);
  await page.waitForFunction(() => (window as any).testBridge.messages.some((message: any) => message.type === 'observer-error'));
  await page.mouse.move(650, 400);
  await page.mouse.down();
  await page.mouse.move(800, 470, { steps: 10 });
  await page.mouse.up();
  await page.waitForTimeout(500);
  expect((await page.screenshot()).equals(restoredPlane)).toBe(false);
  await page.evaluate(snapshots => (window as any).testBridge.receive({ data: { command: 'set-trajectories', snapshots: snapshots.slice(0, 3), requestId: 'three-targets' } }), selectedSnapshots);
  await expect(page.locator('#trajectory-info')).toContainText('3 selected satellites');
  await expect(page.locator('#trajectory-info')).not.toContainText('LARES-2');
  await page.evaluate(() => (window as any).testBridge.receive({ data: { command: 'set-trajectories', snapshots: [], requestId: 'none' } }));
  await expect(page.locator('#trajectory-info')).toBeHidden();
  expect(errors).toEqual([]);
  expect(externalRequests).toEqual([]);
});
