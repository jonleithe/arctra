import { test, expect } from '@playwright/test';
import { validateLiveFrame } from '../src/live';

const epoch = '2026-11-16T10:00:00Z';
const now = Date.parse(epoch);
const valid = () => ({ schemaVersion: 1, ok: true, epochUtc: epoch,
  observer: { latitudeDeg: 78.923, longitudeDeg: 11.923, ellipsoidalHeightM: 0, minElevationDeg: 0 }, targets: [{
  key: 'lageos1', name: 'LAGEOS-1', state: 'ready', positionEcefM: [12_000_000, 0, 0],
  cpf: { filename: 'lageos1_cpf.dgf', provider: 'DGF', referenceFrame: 0, usableStartUtc: '2026-11-16T09:00:00Z', usableEndUtc: '2026-11-16T11:00:00Z', sourceUrl: 'https://example.test/cpf', retrievedAtUtc: epoch },
  look: { azimuthDeg: 90, elevationDeg: 10, slantRangeM: 8_000_000, aboveHorizon: true, aboveThreshold: true },
}] });

test('live frames require fresh epochs, valid CPF coverage and authoritative geometry', () => {
  expect(validateLiveFrame(valid(), now).targets).toHaveLength(1);
  expect(() => validateLiveFrame(valid(), now + 4000)).toThrow();
  expect(() => validateLiveFrame(valid(), now - 3000)).toThrow();
  const outside = valid(); outside.targets[0].cpf.usableEndUtc = '2026-11-16T09:59:59Z';
  expect(() => validateLiveFrame(outside, now)).toThrow();
  const incorrectUnits = valid(); incorrectUnits.targets[0].positionEcefM = [12000, 0, 0];
  expect(() => validateLiveFrame(incorrectUnits, now)).toThrow();
  const expired = valid(); expired.targets[0].state = 'expired';
  expect(() => validateLiveFrame(expired, now)).toThrow();
  const wrongLimit = valid(); wrongLimit.observer.minElevationDeg = 12.5;
  expect(() => validateLiveFrame(wrongLimit, now)).toThrow();
  wrongLimit.targets[0].look.aboveThreshold = false;
  expect(validateLiveFrame(wrongLimit, now).targets[0].look!.aboveThreshold).toBe(false);
});

test('one-second live frames render independently of geometry and fail closed on interruption', async ({ page }) => {
  await page.clock.install({ time: new Date(epoch) });
  await page.addInitScript(() => {
    Object.assign(window, { testBridge: { messages: [], receive: () => {} }, chrome: { webview: {
      postMessage: (message: unknown) => (window as any).testBridge.messages.push(message),
      addEventListener: (_type: string, listener: unknown) => (window as any).testBridge.receive = listener,
    } } });
  });
  await page.goto('http://127.0.0.1:4173');
  await page.waitForFunction(() => (window as any).testBridge.messages.some((message: any) => message.type === 'ready'));
  await page.evaluate(() => (window as any).testBridge.receive({ data: { command: 'begin-live', keys: ['lageos1'] } }));
  const first = valid();
  await page.evaluate(frame => (window as any).testBridge.receive({ data: { command: 'live-frame', frame } }), first);
  await expect(page.locator('#trajectory-info')).toHaveAttribute('data-live-epoch', epoch);
  await page.clock.runFor(1000);
  const next = valid(); next.epochUtc = '2026-11-16T10:00:01Z'; next.targets[0].positionEcefM = [12_000_000, 5000, 0];
  await page.evaluate(frame => (window as any).testBridge.receive({ data: { command: 'live-frame', frame } }), next);
  await expect(page.locator('#trajectory-info')).toHaveAttribute('data-live-epoch', next.epochUtc);
  await expect(page.locator('#trajectory-info')).toContainText('1 of 1 ready');
  await page.clock.runFor(4500);
  await expect(page.locator('#trajectory-info')).toContainText('interrupted');
  await expect(page.locator('#trajectory-info')).not.toHaveAttribute('data-live-epoch');
  await page.evaluate(() => (window as any).testBridge.receive({ data: { command: 'begin-live', keys: [] } }));
  await expect(page.locator('#trajectory-info')).toContainText('Select satellites');
});
