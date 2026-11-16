import {
  ArcType, Cartesian2, Cartesian3, Color, Ellipsoid, Entity, HeadingPitchRange,
  HorizontalOrigin, LabelStyle, Math as CesiumMath, Matrix3, Matrix4, Plane,
  Quaternion, Transforms, Viewer,
  BoundingSphere, ColorGeometryInstanceAttribute, ComponentDatatype, Geometry,
  GeometryAttribute, GeometryAttributes, GeometryInstance, PerInstanceColorAppearance, Primitive, PrimitiveType,
} from 'cesium';

export interface ObserverSettings {
  name: string;
  latitudeDeg: number;
  longitudeDeg: number;
  ellipsoidalHeightM: number;
  planeWidthKm: number;
  showPlane: boolean;
  minElevationDeg?: number;
  showElevationCone?: boolean;
}
export const defaultObserver: ObserverSettings = {
  name: 'Ny-Ålesund', latitudeDeg: 78.923, longitudeDeg: 11.923,
  ellipsoidalHeightM: 0, planeWidthKm: 12000, showPlane: true, minElevationDeg: 0, showElevationCone: true,
};

export function validateObserver(value: unknown): ObserverSettings {
  if (!value || typeof value !== 'object') throw new Error('Observer settings are missing.');
  const observer = value as ObserverSettings;
  if (typeof observer.name !== 'string' || !observer.name.trim() || observer.name.trim().length > 80)
    throw new Error('Observer name must contain 1–80 characters.');
  for (const [value, minimum, maximum, label] of [
    [observer.latitudeDeg, -90, 90, 'Latitude'],
    [observer.longitudeDeg, -180, 180, 'Longitude'],
    [observer.ellipsoidalHeightM, -1000, 100000, 'Ellipsoidal height'],
    [observer.planeWidthKm, 500, 40000, 'Plane width'],
  ] as const) {
    if (typeof value !== 'number' || !Number.isFinite(value) || value < minimum || value > maximum)
      throw new Error(`${label} must be between ${minimum} and ${maximum}.`);
  }
  if (typeof observer.showPlane !== 'boolean') throw new Error('Plane visibility must be true or false.');
  const minimum = observer.minElevationDeg ?? 0;
  if (!Number.isFinite(minimum) || minimum < -90 || minimum > 90) throw new Error('Minimum elevation must be between -90 and 90 degrees.');
  if (observer.showElevationCone !== undefined && typeof observer.showElevationCone !== 'boolean') throw new Error('Invalid elevation-cone visibility.');
  return { ...observer, name: observer.name.trim(), minElevationDeg: minimum, showElevationCone: observer.showElevationCone ?? true };
}

// Cesium's WGS 84 ENU frame is used only to place rendering geometry. This
// module does not calculate satellite visibility, look angles or predictions.
export function observerFrame(observer: ObserverSettings): Matrix4 {
  const surface = Cartesian3.fromDegrees(observer.longitudeDeg, observer.latitudeDeg, 0, Ellipsoid.WGS84);
  const origin = Cartesian3.fromDegrees(observer.longitudeDeg, observer.latitudeDeg, observer.ellipsoidalHeightM, Ellipsoid.WGS84);
  const frame = Transforms.eastNorthUpToFixedFrame(surface, Ellipsoid.WGS84);
  // Orient using geodetic latitude at the ellipsoid, then translate to the
  // entered height. Height must not change the local geodetic Up direction.
  return Matrix4.setTranslation(frame, origin, frame);
}

// Display bounds only: project supplied geometry into the observer's local
// rendering frame. No satellite state or visibility is calculated here.
export function fittedPlaneWidthM(frame: Matrix4, positions: readonly Cartesian3[], fallbackWidthKm: number): number {
  if (positions.length === 0) return fallbackWidthKm * 1000;
  const inverse = Matrix4.inverseTransformation(frame, new Matrix4());
  const local = new Cartesian3();
  let radius = 0;
  for (const position of positions) {
    Matrix4.multiplyByPoint(inverse, position, local);
    if (!Number.isFinite(local.x) || !Number.isFinite(local.y)) throw new Error('Invalid orbit display bounds.');
    radius = Math.max(radius, Math.hypot(local.x, local.y));
  }
  // An 8% margin surrounds the farthest horizontal projection. Round upward
  // to 100 km to avoid tiny size changes on successive one-second updates.
  return Math.max(500_000, Math.ceil(radius * 2 * 1.08 / 100_000) * 100_000);
}

// A visual threshold boundary, not a trajectory or visibility calculation.
// Points have a constant elevation above the local East–North plane.
export function elevationConeRing(minElevationDeg: number, extentM: number, segments = 96): Cartesian3[] {
  const angle = CesiumMath.toRadians(minElevationDeg);
  const radius = extentM * Math.cos(angle);
  const up = extentM * Math.sin(angle);
  const ring = Array.from({ length: segments }, (_, index) => {
    const bearing = index * CesiumMath.TWO_PI / segments;
    return new Cartesian3(radius * Math.cos(bearing), radius * Math.sin(bearing), up);
  });
  ring.push(Cartesian3.clone(ring[0]));
  return ring;
}

export class ObserverLayer {
  private root?: Entity;
  private frame = Matrix4.clone(Matrix4.IDENTITY);
  private settings = defaultObserver;
  private orbitPositions: readonly Cartesian3[] = [];
  private renderedWidthM = defaultObserver.planeWidthKm * 1000;
  private conePrimitive?: Primitive;

  constructor(private viewer: Viewer) { this.apply(defaultObserver); }

  apply(value: unknown): ObserverSettings {
    const settings = validateObserver(value);
    const frame = observerFrame(settings);
    if (this.conePrimitive) { this.viewer.scene.primitives.remove(this.conePrimitive); this.conePrimitive = undefined; }
    if (this.root) {
      for (const entity of this.viewer.entities.values.filter(entity => entity.parent === this.root || entity.parent?.parent === this.root))
        this.viewer.entities.remove(entity);
      this.viewer.entities.remove(this.root);
    }
    this.settings = settings;
    this.frame = frame;
    this.renderedWidthM = fittedPlaneWidthM(frame, this.orbitPositions, settings.planeWidthKm);
    const origin = Matrix4.getTranslation(frame, new Cartesian3());
    const orientation = Quaternion.fromRotationMatrix(Matrix4.getMatrix3(frame, new Matrix3()));
    const half = this.renderedWidthM / 2;
    const point = (east: number, north: number, up = 0) => Matrix4.multiplyByPoint(frame, new Cartesian3(east, north, up), new Cartesian3());
    const root = this.viewer.entities.add({ id: 'observer', name: settings.name });
    this.root = root;
    const planeRoot = this.viewer.entities.add({ id: 'observer-plane', parent: root, show: settings.showPlane });
    const accent = Color.fromCssColorString('#6CBBD9');
    this.viewer.entities.add({ parent: planeRoot, position: origin, orientation,
      plane: { plane: new Plane(Cartesian3.UNIT_Z, 0), dimensions: new Cartesian2(half * 2, half * 2),
        material: accent.withAlpha(0.12), fill: true },
    });
    const line = (positions: Cartesian3[], color: Color, width: number) => this.viewer.entities.add({ parent: planeRoot,
      polyline: { positions, arcType: ArcType.NONE, clampToGround: false, material: color, width },
    });
    line([point(-half, -half), point(half, -half), point(half, half), point(-half, half), point(-half, -half)], accent.withAlpha(0.7), 1.5);
    for (const fraction of [-0.5, 0.5]) {
      line([point(-half, fraction * half), point(half, fraction * half)], accent.withAlpha(0.2), 1);
      line([point(fraction * half, -half), point(fraction * half, half)], accent.withAlpha(0.2), 1);
    }
    const eastColor = Color.fromCssColorString('#E9BA79');
    const northColor = Color.fromCssColorString('#85D6B5');
    line([point(-half, 0), point(half, 0)], eastColor.withAlpha(0.7), 1.5);
    line([point(0, -half), point(0, half)], northColor.withAlpha(0.7), 1.5);
    line([origin, point(0, 0, half * 0.45)], accent, 2);
    const label = (text: string, position: Cartesian3, color: Color, parent = planeRoot) => this.viewer.entities.add({ parent, position,
      label: { text, font: '12px sans-serif', fillColor: color, style: LabelStyle.FILL_AND_OUTLINE,
        outlineColor: Color.fromCssColorString('#080F1B'), outlineWidth: 3, pixelOffset: new Cartesian2(8, -8), horizontalOrigin: HorizontalOrigin.LEFT },
    });
    label('E · East', point(half * 1.03, 0), eastColor);
    label('N · North', point(0, half * 1.03), northColor);
    label('Up', point(0, 0, half * 0.46), accent);
    this.viewer.entities.add({ parent: root, position: origin,
      point: { pixelSize: 8, color: Color.fromCssColorString('#85D6B5'), outlineColor: Color.fromCssColorString('#080F1B'), outlineWidth: 2 },
    });
    label(settings.name, origin, Color.fromCssColorString('#BFE4DB'), root);
    if (settings.showElevationCone) {
      const minimum = settings.minElevationDeg ?? 0;
      const extent = this.orbitPositions.reduce((maximum, position) => Math.max(maximum, Cartesian3.distance(origin, position) * 1.08), half);
      const ring = elevationConeRing(minimum, extent);
      const coneRoot = this.viewer.entities.add({ id: 'observer-elevation-limit', parent: root });
      const boundaryColor = Color.fromCssColorString('#EBC18A');
      const boundary = Math.abs(minimum) >= 89.999 ? [origin, Matrix4.multiplyByPoint(frame, ring[0], new Cartesian3())]
        : ring.map(position => Matrix4.multiplyByPoint(frame, position, new Cartesian3()));
      this.viewer.entities.add({ parent: coneRoot, polyline: { positions: boundary,
        arcType: ArcType.NONE, clampToGround: false, width: 1.5, material: boundaryColor.withAlpha(0.7) } });
      if (Math.abs(minimum) > 0.001 && Math.abs(minimum) < 89.999) {
        const positions = new Float64Array((ring.length + 1) * 3);
        const indices: number[] = [];
        ring.forEach((position, index) => {
          positions.set([position.x, position.y, position.z], (index + 1) * 3);
          if (index < ring.length - 1) indices.push(0, index + 1, index + 2);
        });
        const attributes = new GeometryAttributes();
        attributes.position = new GeometryAttribute({ componentDatatype: ComponentDatatype.DOUBLE, componentsPerAttribute: 3, values: positions });
        const geometry = new Geometry({ attributes,
          indices: new Uint16Array(indices), primitiveType: PrimitiveType.TRIANGLES, boundingSphere: BoundingSphere.fromVertices(positions) });
        this.conePrimitive = this.viewer.scene.primitives.add(new Primitive({ asynchronous: false,
          geometryInstances: new GeometryInstance({ geometry, modelMatrix: frame, attributes: { color: ColorGeometryInstanceAttribute.fromColor(boundaryColor.withAlpha(0.055)) } }),
          appearance: new PerInstanceColorAppearance({ flat: true, translucent: true, closed: false }),
        }));
      }
      label(`Minimum elevation ${minimum}°`, Matrix4.multiplyByPoint(frame, ring[12], new Cartesian3()), boundaryColor, coneRoot);
    }
    this.viewer.scene.requestRender();
    return settings;
  }

  setOrbitPositions(positions: readonly Cartesian3[]) {
    const origin = Matrix4.getTranslation(this.frame, new Cartesian3());
    const previousExtent = this.orbitPositions.reduce((maximum, position) => Math.max(maximum, Cartesian3.distance(origin, position)), 0);
    const nextExtent = positions.reduce((maximum, position) => Math.max(maximum, Cartesian3.distance(origin, position)), 0);
    this.orbitPositions = positions;
    const width = fittedPlaneWidthM(this.frame, positions, this.settings.planeWidthKm);
    if (width !== this.renderedWidthM || Math.ceil(previousExtent / 100_000) !== Math.ceil(nextExtent / 100_000)) this.apply(this.settings);
  }

  matchesObserver(site: { latitudeDeg: number; longitudeDeg: number; ellipsoidalHeightM: number; minElevationDeg: number }) {
    return this.settings.latitudeDeg === site.latitudeDeg && this.settings.longitudeDeg === site.longitudeDeg &&
      this.settings.ellipsoidalHeightM === site.ellipsoidalHeightM && (this.settings.minElevationDeg ?? 0) === site.minElevationDeg;
  }

  focus(side = false) {
    const range = Math.max(side ? 36_000_000 : 12_000_000, this.renderedWidthM * (side ? 2.7 : 1.3));
    // Aim a little below the horizon in the side view so Earth and orbital
    // loops fit in the viewport together, with the plane above Earth's centre.
    const targetFrame = side ? Matrix4.setTranslation(this.frame,
      Matrix4.multiplyByPoint(this.frame, new Cartesian3(0, 0, -3_000_000), new Cartesian3()), new Matrix4()) : this.frame;
    this.viewer.camera.lookAtTransform(targetFrame, new HeadingPitchRange(
      CesiumMath.toRadians(side ? 135 : 0), CesiumMath.toRadians(side ? -7 : -75), range,
    ));
    // Release the local camera transform so normal free globe rotation remains
    // available after either preset. The plane itself stays fixed to Earth.
    this.viewer.camera.lookAtTransform(Matrix4.IDENTITY);
    this.viewer.scene.requestRender();
  }
}
