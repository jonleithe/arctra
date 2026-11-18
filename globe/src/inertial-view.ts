// The CPF files supported by Arctra contain Earth-fixed coordinates.  The
// globe worker's orbitView rotates each time-tagged sample about Z into axes
// frozen at its reference epoch.  These helpers apply the inverse purely
// visual rotation when drawing that frozen path back in Cesium's Earth-fixed
// scene.  UTC is deliberately used as the UT1 approximation, matching the
// worker's documented contextual orbit-view model.

export const nominalEarthRotationRadPerSecond = 7.2921150e-5;

export type Cartesian = readonly [number, number, number];

export function nominalEarthRotationAngle(referenceEpochMs: number, epochMs: number): number {
  return nominalEarthRotationRadPerSecond * (epochMs - referenceEpochMs) / 1000;
}

function rotateAboutZ([x, y, z]: Cartesian, angleRad: number): [number, number, number] {
  const cosine = Math.cos(angleRad);
  const sine = Math.sin(angleRad);
  return [cosine * x - sine * y, sine * x + cosine * y, z];
}

// Matches GlobeWorker::display_position: frame-0 ECEF at sample epoch to the
// display axes frozen at referenceEpochMs.
export function ecefToEpochFrozen(position: Cartesian, referenceEpochMs: number, sampleEpochMs: number): [number, number, number] {
  return rotateAboutZ(position, nominalEarthRotationAngle(referenceEpochMs, sampleEpochMs));
}

// Places a frozen display coordinate into Cesium's Earth-fixed scene at the
// currently displayed epoch.  At that epoch it exactly recovers the original
// CPF ECEF position.
export function epochFrozenToEcef(position: Cartesian, referenceEpochMs: number, displayEpochMs: number): [number, number, number] {
  return rotateAboutZ(position, -nominalEarthRotationAngle(referenceEpochMs, displayEpochMs));
}
