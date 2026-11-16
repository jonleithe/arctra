import Ajv from 'ajv';
import addFormats from 'ajv-formats';
import schema from '../../contracts/schemas/trajectory-snapshot-v1.schema.json' with { type: 'json' };

export interface TrajectorySnapshot {
  schemaVersion: 1;
  predictionKind: 'cpf-ecef-geometric';
  displayMode: 'recorded-preview' | 'live';
  generatedAtUtc: string;
  epochUtc: string;
  target: { key: string; name: string; noradId: number; ilrsId: number };
  cpf: { provider: string; filename: string; sourceUrl: string; retrievedAtUtc: string; referenceFrame: 0; usableStartUtc: string; usableEndUtc: string };
  trajectory: { sampleStepSeconds: number; samples: { epochUtc: string; positionEcefM: [number, number, number] }[] };
  referenceOrbit?: { model: 'approximate-two-body'; frame: 'epoch-frozen-terrestrial'; epochUtc: string;
    semiMajorAxisM: number; eccentricity: number; periodSeconds: number; positionsM: [number, number, number][] };
}
const ajv = new Ajv({ strict: true });
addFormats(ajv);
const validateSchema = ajv.compile<TrajectorySnapshot>(schema);

export function validateSnapshot(value: unknown): TrajectorySnapshot {
  if (!validateSchema(value)) throw new Error(`Invalid trajectory snapshot: ${ajv.errorsText(validateSchema.errors)}`);
  const start = Date.parse(value.cpf.usableStartUtc);
  const end = Date.parse(value.cpf.usableEndUtc);
  if (!(start < end)) throw new Error('Invalid CPF coverage interval.');
  let previous: number | undefined;
  for (const sample of value.trajectory.samples) {
    const epoch = Date.parse(sample.epochUtc);
    if (epoch < start || epoch > end) throw new Error('Trajectory sample is outside CPF coverage.');
    if (previous !== undefined && epoch - previous !== value.trajectory.sampleStepSeconds * 1000)
      throw new Error('Trajectory contains unordered samples or a data gap.');
    if (!sample.positionEcefM.every(Number.isFinite)) throw new Error('Nonfinite ECEF coordinate.');
    // Guard the unit boundary without performing any orbit calculation.
    if (Math.hypot(...sample.positionEcefM) < 6_300_000) throw new Error('ECEF position is inside Earth; check metre units.');
    previous = epoch;
  }
  if (value.epochUtc !== value.trajectory.samples[0].epochUtc)
    throw new Error('Preview marker must use the first supplied sample epoch.');
  if (value.referenceOrbit) {
    const orbit = value.referenceOrbit;
    const points = orbit.positionsM;
    const marker = value.trajectory.samples[0].positionEcefM;
    if (orbit.epochUtc !== value.epochUtc || points[0].some((coordinate, index) => coordinate !== points.at(-1)![index] || coordinate !== marker[index]))
      throw new Error('Reference orbit must close exactly at the CPF preview marker and epoch.');
    if (points.some(point => !point.every(Number.isFinite) || Math.hypot(...point) < 6_378_137))
      throw new Error('Invalid reference orbit coordinates.');
  }
  return value;
}
