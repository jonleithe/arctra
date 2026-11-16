import { test, expect } from '@playwright/test';
import { Cartesian3, Matrix4 } from 'cesium';
import { defaultObserver, elevationConeRing, fittedPlaneWidthM, observerFrame, validateObserver } from '../src/observer';

test('WGS 84 observer rendering frame is tangent at the equator and follows height', () => {
  const settings = { ...defaultObserver, latitudeDeg: 0, longitudeDeg: 0, ellipsoidalHeightM: 1200 };
  const frame = observerFrame(settings);
  const origin = Matrix4.getTranslation(frame, new Cartesian3());
  expect(origin.x).toBeCloseTo(6378137 + 1200, 5);
  expect(origin.y).toBeCloseTo(0, 5);
  expect(origin.z).toBeCloseTo(0, 5);
  expect(Matrix4.multiplyByPointAsVector(frame, Cartesian3.UNIT_X, new Cartesian3())).toEqual(new Cartesian3(0, 1, 0));
  expect(Matrix4.multiplyByPointAsVector(frame, Cartesian3.UNIT_Y, new Cartesian3())).toEqual(new Cartesian3(0, 0, 1));
  expect(Matrix4.multiplyByPointAsVector(frame, Cartesian3.UNIT_Z, new Cartesian3())).toEqual(new Cartesian3(1, 0, 0));
});

test('rendering frames remain orthogonal at poles and across hemispheres', () => {
  for (const [latitudeDeg, longitudeDeg] of [[90, 0], [-90, 180], [-31.95, 115.86], [78.923, 11.923], [0, -180]]) {
    const frame = observerFrame({ ...defaultObserver, latitudeDeg, longitudeDeg });
    const east = Matrix4.multiplyByPointAsVector(frame, Cartesian3.UNIT_X, new Cartesian3());
    const north = Matrix4.multiplyByPointAsVector(frame, Cartesian3.UNIT_Y, new Cartesian3());
    const up = Matrix4.multiplyByPointAsVector(frame, Cartesian3.UNIT_Z, new Cartesian3());
    expect(Cartesian3.dot(east, north)).toBeCloseTo(0, 12);
    expect(Cartesian3.dot(east, up)).toBeCloseTo(0, 12);
    expect(Cartesian3.dot(north, up)).toBeCloseTo(0, 12);
    expect(Cartesian3.magnitude(up)).toBeCloseTo(1, 12);
    const elevated = observerFrame({ ...defaultObserver, latitudeDeg, longitudeDeg, ellipsoidalHeightM: 5000 });
    expect(Matrix4.multiplyByPointAsVector(elevated, Cartesian3.UNIT_Z, new Cartesian3())).toEqual(up);
  }
});

test('observer messages reject malformed coordinates without silently wrapping', () => {
  for (const mutation of [{ latitudeDeg: 91 }, { longitudeDeg: 181 }, { ellipsoidalHeightM: NaN }, { planeWidthKm: 0 }, { name: '' }, { showPlane: 1 }])
    expect(() => validateObserver({ ...defaultObserver, ...mutation })).toThrow();
});

test('plane bounds grow beyond distant orbits and shrink on deselection without a 40000 km cap', () => {
  const frame = observerFrame(defaultObserver);
  const point = (east: number, north: number, up: number) => Matrix4.multiplyByPoint(frame, new Cartesian3(east, north, up), new Cartesian3());
  const nearby = [point(8_000_000, 0, 1_000_000), point(-8_000_000, 0, -1_000_000)];
  const distant = [point(45_000_000, 0, 5_000_000), point(0, -43_000_000, 0)];
  const smallWidth = fittedPlaneWidthM(frame, nearby, 12000);
  const largeWidth = fittedPlaneWidthM(frame, nearby.concat(distant), 12000);
  expect(largeWidth).toBeGreaterThan(90_000_000);
  expect(largeWidth).toBeGreaterThan(smallWidth);
  expect(fittedPlaneWidthM(frame, nearby, 12000)).toBe(smallWidth);
  expect(fittedPlaneWidthM(frame, [], 12000)).toBe(12_000_000);
  const inverse = Matrix4.inverseTransformation(frame, new Matrix4());
  for (const position of nearby.concat(distant)) {
    const local = Matrix4.multiplyByPoint(inverse, position, new Cartesian3());
    expect(Math.hypot(local.x, local.y) * 1.08).toBeLessThanOrEqual(largeWidth / 2);
  }
  const movedFrame = observerFrame({ ...defaultObserver, latitudeDeg: -31.95, longitudeDeg: 115.86 });
  expect(fittedPlaneWidthM(movedFrame, nearby.concat(distant), 12000)).not.toBe(largeWidth);
});

test('minimum-elevation boundary has the configured angle in every direction, including limits', () => {
  for (const minimum of [-90, -5, 0, 12.5, 45, 89.9, 90]) {
    const ring = elevationConeRing(minimum, 20_000_000);
    expect(ring[0]).toEqual(ring.at(-1));
    for (const position of ring) {
      expect(Math.atan2(position.z, Math.hypot(position.x, position.y)) * 180 / Math.PI).toBeCloseTo(minimum, 10);
      expect(Cartesian3.magnitude(position)).toBeCloseTo(20_000_000, 5);
    }
  }
});
