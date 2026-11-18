import { test, expect } from '@playwright/test';
import { ecefToEpochFrozen, epochFrozenToEcef, nominalEarthRotationAngle } from '../src/inertial-view';

test('the nominal display rotations round-trip an ECEF CPF coordinate', () => {
  const reference = Date.parse('2026-11-18T12:00:00Z');
  const sample = reference + 5_400_000;
  const ecef: [number, number, number] = [12_345_678.9, -7_654_321.0, 3_456_789.1];
  const frozen = ecefToEpochFrozen(ecef, reference, sample);
  const restored = epochFrozenToEcef(frozen, reference, sample);

  expect(restored[0]).toBeCloseTo(ecef[0], 6);
  expect(restored[1]).toBeCloseTo(ecef[1], 6);
  expect(restored[2]).toBeCloseTo(ecef[2], 6);
  expect(nominalEarthRotationAngle(reference, sample)).toBeCloseTo(0.39377421, 8);
});
