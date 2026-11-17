import { test, expect } from '@playwright/test';
import { readFileSync } from 'node:fs';

const snapshot = JSON.parse(readFileSync(
  new URL('../fixtures/lageos1-trajectory-v1.json', import.meta.url), 'utf8'));
const classifiedSnapshot = structuredClone(snapshot);
classifiedSnapshot.trajectory.visibility = {
  observer: { latitudeDeg: 78.923, longitudeDeg: 11.923, ellipsoidalHeightM: 50, minElevationDeg: 10 },
  endReason: 'visible-pass-complete',
  aboveThresholdSegments: [{ samples: classifiedSnapshot.trajectory.samples.slice(30, 80) }],
};

test('standalone browser requests and toggles sampled CPF trajectories', async ({ page }) => {
  test.setTimeout(30_000);
  await page.clock.install({ time: new Date(snapshot.epochUtc) });
  await page.addInitScript(() => localStorage.setItem(
    'arctra-selected-targets-v1', JSON.stringify(['etalon1'])));
  await page.route('**/startup.json', route => route.fulfill({ json: {
    observer: { name: 'CLI Observatory', latitudeDeg: 78.923, longitudeDeg: 11.923,
      ellipsoidalHeightM: 50, minElevationDeg: 10 },
    initialTargets: ['lageos1'], overrideSavedTargets: true,
  } }));
  const requests: any[] = [];
  await page.route('**/api/live', async route => {
    const request = route.request().postDataJSON();
    requests.push(request);
    const target = request.targets[0];
    const frame: any = {
      schemaVersion: 1, ok: true, epochUtc: snapshot.epochUtc,
      observer: {
        latitudeDeg: request.observer.latitudeDeg,
        longitudeDeg: request.observer.longitudeDeg,
        ellipsoidalHeightM: request.observer.ellipsoidalHeightM,
        minElevationDeg: request.observer.minElevationDeg,
      },
      targets: [{ key: target.key, name: target.name, state: 'ready',
        positionEcefM: snapshot.trajectory.samples[0].positionEcefM,
        cpf: snapshot.cpf,
        look: { azimuthDeg: 90, elevationDeg: 20, slantRangeM: 8_000_000,
          aboveHorizon: true, aboveThreshold: true } }],
    };
    if (request.includeTrajectories) frame.snapshots = [classifiedSnapshot];
    await route.fulfill({ json: frame });
  });

  await page.goto('http://127.0.0.1:4173');
  await expect(page.locator('#observer-name')).toHaveValue('CLI Observatory');
  await expect(page.locator('#target-count')).toContainText('1 selected');
  await expect(page.locator('.target').filter({ hasText: 'LAGEOS-1' }).locator('input')).toBeChecked();
  await expect(page.locator('.target').filter({ hasText: 'ETALON-1' }).locator('input')).not.toBeChecked();
  const toggle = page.locator('#trajectory-toggle');
  await expect(toggle).toBeChecked();
  await expect.poll(() => requests.length).toBeGreaterThan(0);
  expect(requests[0].includeTrajectories).toBe(true);
  await expect(page.locator('#trajectory-info')).toContainText('LIVE CPF predictions / 1 of 1 ready');
  await expect(page.locator('#trajectory-info')).toHaveAttribute('data-trajectory-visible', 'true');
  await toggle.uncheck();
  await expect(page.locator('#trajectory-info')).toHaveAttribute('data-trajectory-visible', 'false');
  await toggle.check();
  await expect(page.locator('#trajectory-info')).toHaveAttribute('data-trajectory-visible', 'true');
});
