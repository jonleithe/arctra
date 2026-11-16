import { test, expect } from '@playwright/test';
import { readFileSync } from 'node:fs';
import { validateSnapshot } from '../src/snapshot';

const snapshot = JSON.parse(readFileSync(new URL('../fixtures/lageos1-trajectory-v1.json', import.meta.url), 'utf8'));

test('accepts the real exported fixture with explicit coverage and metre units', () => {
  const validated = validateSnapshot(snapshot);
  expect(validated.trajectory.samples).toHaveLength(481);
  expect(validated.target.key).toBe('lageos1');
  expect(validated.cpf.filename).toBe('lageos1_cpf_261115_31902.dgf');
  expect(validated.referenceOrbit!.positionsM).toHaveLength(361);
  expect(validated.referenceOrbit!.positionsM[0]).toEqual(validated.referenceOrbit!.positionsM.at(-1));
  expect(validated.referenceOrbit!.positionsM[0]).toEqual(validated.trajectory.samples[0].positionEcefM);
});

test('all four selected targets have valid closed reference loops', () => {
  for (const key of ['lageos1', 'lageos2', 'lares', 'lares2']) {
    const value = JSON.parse(readFileSync(new URL(`../fixtures/${key}-trajectory-v1.json`, import.meta.url), 'utf8'));
    expect(validateSnapshot(value).target.key).toBe(key);
  }
});

test('rejects incompatible, out-of-coverage, unordered and incorrectly scaled snapshots', () => {
  const mutate = (action: (copy: any) => void) => {
    const copy = structuredClone(snapshot);
    action(copy);
    expect(() => validateSnapshot(copy)).toThrow();
  };
  mutate(copy => copy.schemaVersion = 2);
  mutate(copy => delete copy.cpf.provider);
  mutate(copy => copy.cpf.referenceFrame = 1);
  mutate(copy => copy.trajectory.samples[0].epochUtc = '2026-11-01T00:00:00Z');
  mutate(copy => copy.trajectory.samples[1].epochUtc = copy.trajectory.samples[0].epochUtc);
  mutate(copy => copy.trajectory.samples[1].epochUtc = '2026-11-16T09:02:00Z');
  mutate(copy => copy.trajectory.samples[0].positionEcefM = [12_000, 0, 0]);
  mutate(copy => copy.trajectory.samples[0].positionEcefM[0] = Infinity);
  mutate(copy => copy.epochUtc = '2026-11-16T10:00:00Z');
  mutate(copy => copy.referenceOrbit.positionsM[360][0] += 1);
  mutate(copy => copy.referenceOrbit.epochUtc = '2026-11-16T10:00:00Z');
  mutate(copy => copy.referenceOrbit.positionsM[10][0] = Infinity);
});
