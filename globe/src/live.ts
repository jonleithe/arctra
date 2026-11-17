export interface LiveState {
  key: string;
  name: string;
  state: 'ready' | 'loading' | 'error' | 'expired' | 'outside-coverage';
  message?: string;
  positionEcefM?: [number, number, number];
  cpf?: { filename: string; provider: string; usableStartUtc: string; usableEndUtc: string; sourceUrl: string; retrievedAtUtc?: string; referenceFrame: 0 };
  look?: { azimuthDeg: number; elevationDeg: number; slantRangeM: number; aboveHorizon: boolean; aboveThreshold: boolean };
}
export interface LiveFrame { schemaVersion: 1; ok: true; epochUtc: string; observer: { latitudeDeg: number; longitudeDeg: number; ellipsoidalHeightM: number; minElevationDeg: number }; targets: LiveState[]; snapshots?: unknown[]; }
export const maximumLiveAgeMs = 3500;
const ajv = new Ajv({ strict: true });
addFormats(ajv);
ajv.addSchema(trajectorySchema);
const validateSchema = ajv.compile<LiveFrame>(schema);
export function validateLiveFrame(value: unknown, nowMs = Date.now()): LiveFrame {
  if (!validateSchema(value)) throw new Error(`Invalid live frame: ${ajv.errorsText(validateSchema.errors)}`);
  const frame = value as LiveFrame;
  if (!frame || frame.schemaVersion !== 1 || typeof frame.epochUtc !== 'string' || !Array.isArray(frame.targets)) throw new Error('Invalid live frame.');
  const epoch = Date.parse(frame.epochUtc);
  if (!Number.isFinite(epoch) || epoch > nowMs + 2000 || nowMs - epoch > maximumLiveAgeMs) throw new Error('Live prediction frame is stale or ahead of the clock.');
  if (new Set(frame.targets.map(target => target.key)).size !== frame.targets.length) throw new Error('Duplicate live targets.');
  for (const target of frame.targets) {
    if (typeof target.key !== 'string' || !/^[a-z0-9]+$/.test(target.key) || typeof target.name !== 'string' ||
      !['ready', 'loading', 'error', 'expired', 'outside-coverage'].includes(target.state)) throw new Error('Invalid live target.');
    if (target.state !== 'ready') {
      if (target.positionEcefM !== undefined || target.look !== undefined) throw new Error('Invalid target must not carry a live position.');
      continue;
    }
    const cpf = target.cpf;
    const position = target.positionEcefM;
    const look = target.look;
    if (!cpf || cpf.referenceFrame !== 0 || typeof cpf.filename !== 'string' || !cpf.filename || typeof cpf.provider !== 'string' ||
      !Number.isFinite(Date.parse(cpf.usableStartUtc)) || !Number.isFinite(Date.parse(cpf.usableEndUtc)) ||
      epoch < Date.parse(cpf.usableStartUtc) || epoch > Date.parse(cpf.usableEndUtc)) throw new Error('Live position is outside valid CPF coverage.');
    if (!Array.isArray(position) || position.length !== 3 || !position.every(Number.isFinite) || Math.hypot(...position) < 6_300_000) throw new Error('Invalid live ECEF metre position.');
    if (!look || !Number.isFinite(look.azimuthDeg) || look.azimuthDeg < 0 || look.azimuthDeg >= 360 ||
      !Number.isFinite(look.elevationDeg) || Math.abs(look.elevationDeg) > 90 || !Number.isFinite(look.slantRangeM) || look.slantRangeM <= 0 ||
      look.aboveHorizon !== (look.elevationDeg >= 0) || look.aboveThreshold !== (look.elevationDeg >= frame.observer.minElevationDeg)) throw new Error('Invalid backend look geometry.');
  }
  if (frame.snapshots) {
    const snapshots = frame.snapshots.map(validateSnapshot);
    if (new Set(snapshots.map(snapshot => snapshot.target.key)).size !== snapshots.length)
      throw new Error('Duplicate live trajectory snapshot.');
    for (const snapshot of snapshots) {
      const target = frame.targets.find(candidate => candidate.key === snapshot.target.key);
      if (!target || target.state !== 'ready') throw new Error('Live trajectory has no ready target.');
      const observer = snapshot.trajectory.visibility?.observer;
      if (!observer || observer.latitudeDeg !== frame.observer.latitudeDeg ||
          observer.longitudeDeg !== frame.observer.longitudeDeg ||
          observer.ellipsoidalHeightM !== frame.observer.ellipsoidalHeightM ||
          observer.minElevationDeg !== frame.observer.minElevationDeg)
        throw new Error('Live trajectory visibility uses a different observer.');
    }
  }
  return frame;
}
import Ajv from 'ajv';
import addFormats from 'ajv-formats';
import schema from '../../contracts/schemas/live-state-v1.schema.json' with { type: 'json' };
import trajectorySchema from '../../contracts/schemas/trajectory-snapshot-v1.schema.json' with { type: 'json' };
import { validateSnapshot } from './snapshot';
