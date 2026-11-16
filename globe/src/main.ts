import {
  Viewer, Cartesian3, Color, Math as CesiumMath, TileMapServiceImageryProvider,
  buildModuleUrl, ImageryLayer, EllipsoidTerrainProvider,
  Cartesian2,
  ArcType, Entity, DirectionalLight,
  LabelStyle, HorizontalOrigin, PerspectiveFrustum, ConstantPositionProperty, ConstantProperty,
} from 'cesium';
import { validateSnapshot } from './snapshot';
import { ObserverLayer } from './observer';
import { validateLiveFrame, maximumLiveAgeMs } from './live';
import 'cesium/Build/Cesium/Widgets/widgets.css';
import './style.css';

declare global {
  interface Window {
    CESIUM_BASE_URL: string;
    chrome?: { webview?: {
      postMessage: (message: unknown) => void;
      addEventListener: (type: 'message', listener: (event: MessageEvent) => void) => void;
    } };
  }
}
window.CESIUM_BASE_URL = new URL('./cesium/', document.baseURI).href;
// Explicit resource root: all imagery, workers and textures are local.
const report = (message: unknown) => window.chrome?.webview?.postMessage(message);
function fail(error: unknown) {
  const message = error instanceof Error ? error.message : String(error);
  const panel = document.querySelector<HTMLDivElement>('#error')!;
  panel.textContent = `Unable to render the globe: ${message}`;
  panel.hidden = false;
  report({ type: 'error', message });
}
window.addEventListener('error', event => fail(event.error ?? event.message));
window.addEventListener('unhandledrejection', event => fail(event.reason));

async function start() {
  const imagery = await TileMapServiceImageryProvider.fromUrl(
    buildModuleUrl('Assets/Textures/NaturalEarthII'),
  );
  const earthImagery = new ImageryLayer(imagery, { brightness: 0.96, contrast: 1.12, saturation: 1.12, gamma: 0.95 });
  const viewer = new Viewer('globe', {
    baseLayer: earthImagery,
    terrainProvider: new EllipsoidTerrainProvider(),
    animation: false, timeline: false, baseLayerPicker: false,
    geocoder: false, homeButton: false, sceneModePicker: false,
    navigationHelpButton: false, fullscreenButton: false,
    selectionIndicator: false, infoBox: false,
    requestRenderMode: true,
  });
  viewer.scene.backgroundColor = Color.fromCssColorString('#080F1B');
  if (viewer.scene.skyBox) viewer.scene.skyBox.show = false;
  if (viewer.scene.sun) viewer.scene.sun.show = false;
  if (viewer.scene.moon) viewer.scene.moon.show = false;
  viewer.scene.globe.showGroundAtmosphere = false;
  viewer.scene.globe.enableLighting = true;
  viewer.scene.globe.depthTestAgainstTerrain = true;
  // Illustrative camera-relative lighting makes the sphere readable. This is
  // deliberately not a display of the physical Sun or satellite illumination.
  const light = new DirectionalLight({ direction: new Cartesian3(0, 0, -1), intensity: 1.7 });
  viewer.scene.light = light;
  viewer.scene.preRender.addEventListener(() => {
    const offset = Cartesian3.add(
      Cartesian3.multiplyByScalar(viewer.camera.rightWC, 0.35, new Cartesian3()),
      Cartesian3.multiplyByScalar(viewer.camera.upWC, -0.3, new Cartesian3()), new Cartesian3());
    Cartesian3.normalize(Cartesian3.add(viewer.camera.directionWC, offset, light.direction), light.direction);
  });
  viewer.scene.postProcessStages.fxaa.enabled = true;
  viewer.scene.globe.baseColor = Color.fromCssColorString('#15354D');
  viewer.scene.screenSpaceCameraController.minimumZoomDistance = 1000;
  viewer.scene.renderError.addEventListener((_scene, error) => fail(error));

  const observerLayer = new ObserverLayer(viewer);
  const grid = viewer.entities.add({ name: 'Latitude / longitude grid', show: false });
  for (let longitude = -180; longitude < 180; longitude += 30) {
    const points: number[] = [];
    for (let latitude = -90; latitude <= 90; latitude += 2) points.push(longitude, latitude);
    viewer.entities.add({ parent: grid, polyline: {
      positions: Cartesian3.fromDegreesArray(points), width: 1,
      material: Color.WHITE.withAlpha(0.25),
    } });
  }
  for (let latitude = -60; latitude <= 60; latitude += 30) {
    const points: number[] = [];
    for (let longitude = -180; longitude <= 180; longitude += 2) points.push(longitude, latitude);
    viewer.entities.add({ parent: grid, polyline: {
      positions: Cartesian3.fromDegreesArray(points), width: 1,
      material: Color.WHITE.withAlpha(0.25),
    } });
  }
  function globalView(fly = true) {
    const options = {
      destination: Cartesian3.fromDegrees(15, 54, 20_000_000),
      orientation: { heading: 0, pitch: -CesiumMath.PI_OVER_TWO, roll: 0 },
    };
    if (fly) viewer.camera.flyTo({ ...options, duration: 1.6 });
    else viewer.camera.setView(options);
  }
  globalView(false);
  let trajectoryRoots: Entity[] = [];
  let trajectoryLines: Entity[] = [];
  let referenceLines: Entity[] = [];
  let endMarkers: Entity[] = [];
  let trajectoryPositions: Cartesian3[] = [];
  let frameBasis: Cartesian3[] = [];
  const orbitBounds = new Map<string, Cartesian3[]>();
  let showTrajectory = false;
  let showReference = true;
  const previewPanel = document.querySelector<HTMLDivElement>('#trajectory-info')!;
  let liveMode = false;
  let liveEpochMs = 0;
  let liveReceivedMs = 0;
  let liveKeys = new Set<string>();
  const liveExpiry = new Map<string, number>();
  const palette: Record<string, string> = { lageos1: '#E4B76F', lageos2: '#72C8E7', lares: '#B49BE8', lares2: '#78D3B0' };
  function liveUnavailable(message: string) {
    for (const root of trajectoryRoots) root.show = false;
    observerLayer.setOrbitPositions([]);
    liveEpochMs = 0;
    delete previewPanel.dataset.liveEpoch;
    previewPanel.textContent = message;
    previewPanel.hidden = false;
    viewer.scene.requestRender();
  }
  setInterval(() => {
    if (liveMode) for (const [key, expiry] of liveExpiry) {
      if (Date.now() > expiry) {
        const root = viewer.entities.getById(`prediction-${key}`);
        if (root) root.show = false;
        orbitBounds.delete(key);
        liveExpiry.delete(key);
        viewer.scene.requestRender();
      }
    }
    if (liveMode && liveEpochMs && (Date.now() - liveEpochMs > maximumLiveAgeMs || performance.now() - liveReceivedMs > maximumLiveAgeMs)) {
      liveUnavailable('Live updates interrupted · positions hidden');
      report({ type: 'live-timeout' });
    }
  }, 500);
  function clearTrajectory() {
    for (const entity of viewer.entities.values.filter(entity => entity.parent && trajectoryRoots.includes(entity.parent))) viewer.entities.remove(entity);
    for (const root of trajectoryRoots) viewer.entities.remove(root);
    trajectoryRoots = [];
    trajectoryLines = [];
    referenceLines = [];
    endMarkers = [];
    trajectoryPositions = [];
    frameBasis = [];
    orbitBounds.clear();
    observerLayer.setOrbitPositions([]);
    previewPanel.hidden = true;
    viewer.scene.requestRender();
  }
  function frameTrajectory() {
    if (trajectoryPositions.length < 2) return;
    // Camera framing only: choose an oblique view across the supplied path.
    // Satellite positions themselves are never rotated, propagated or extended.
    const first = Cartesian3.normalize(frameBasis[0], new Cartesian3());
    const quarter = Cartesian3.normalize(frameBasis[Math.floor(frameBasis.length / 4)], new Cartesian3());
    let normal = Cartesian3.cross(first, quarter, new Cartesian3());
    if (Cartesian3.magnitude(normal) < 0.01) normal = Cartesian3.clone(Cartesian3.UNIT_Z);
    Cartesian3.normalize(normal, normal);
    const view = Cartesian3.normalize(Cartesian3.add(normal, Cartesian3.multiplyByScalar(first, 2.2, new Cartesian3()), new Cartesian3()), new Cartesian3());
    const radius = trajectoryPositions.reduce((maximum, position) => Math.max(maximum, Cartesian3.magnitude(position)), 0);
    const direction = Cartesian3.negate(view, new Cartesian3());
    const vertical = Math.abs(Cartesian3.dot(view, Cartesian3.UNIT_Z)) > 0.95 ? Cartesian3.UNIT_Y : Cartesian3.UNIT_Z;
    const right = Cartesian3.normalize(Cartesian3.cross(direction, vertical, new Cartesian3()), new Cartesian3());
    const up = Cartesian3.normalize(Cartesian3.cross(right, direction, new Cartesian3()), new Cartesian3());
    // Keep the complete path inside both the vertical and horizontal field of
    // view, including when the native window is tall or narrow.
    const aspect = viewer.canvas.clientWidth / viewer.canvas.clientHeight;
    const verticalFov = (viewer.camera.frustum instanceof PerspectiveFrustum ? viewer.camera.frustum.fovy : undefined) ?? CesiumMath.toRadians(45);
    const limitingFov = Math.min(verticalFov, 2 * Math.atan(Math.tan(verticalFov / 2) * aspect));
    const distance = radius / Math.sin(limitingFov / 2) * 1.15;
    viewer.camera.flyTo({ destination: Cartesian3.multiplyByScalar(view, distance, new Cartesian3()), orientation: { direction, up }, duration: 1.4 });
  }
  function loadTrajectory(value: unknown, requestId?: string) {
    clearTrajectory();
    try {
      const snapshots = (Array.isArray(value) ? value : [value]).map(validateSnapshot);
      if (new Set(snapshots.map(snapshot => snapshot.target.key)).size !== snapshots.length) throw new Error('Duplicate target snapshots.');
      if (snapshots.some(snapshot => snapshot.epochUtc !== snapshots[0].epochUtc)) throw new Error('Selected reference orbits must share one reference epoch.');
      for (const snapshot of snapshots) {
      const positions = snapshot.trajectory.samples.map(sample => new Cartesian3(...sample.positionEcefM));
      const loop = snapshot.referenceOrbit?.positionsM.map(position => new Cartesian3(...position));
      const color = Color.fromCssColorString(palette[snapshot.target.key] ?? '#E4B76F');
      trajectoryPositions = trajectoryPositions.concat(positions, loop ?? []);
      orbitBounds.set(snapshot.target.key, positions.concat(loop ?? []));
      if (frameBasis.length === 0) frameBasis = loop ?? positions;
      const trajectoryRoot = viewer.entities.add({ id: `prediction-${snapshot.target.key}`, name: snapshot.target.name });
      trajectoryRoots.push(trajectoryRoot);
      trajectoryLines.push(viewer.entities.add({ parent: trajectoryRoot, show: showTrajectory, polyline: {
        positions, width: 1.3, arcType: ArcType.NONE,
        clampToGround: false, material: color.withAlpha(0.4),
      } }));
      if (loop) referenceLines.push(viewer.entities.add({ parent: trajectoryRoot, show: showReference, polyline: {
        positions: loop, width: 2.2, arcType: ArcType.NONE, clampToGround: false, material: color,
      } }));
      viewer.entities.add({ id: `marker-${snapshot.target.key}`, parent: trajectoryRoot, position: positions[0],
        point: { pixelSize: 8, color, outlineColor: Color.fromCssColorString('#080F1B'), outlineWidth: 2 },
        label: { text: snapshot.target.name, font: '13px sans-serif', fillColor: color,
          style: LabelStyle.FILL_AND_OUTLINE, outlineColor: Color.fromCssColorString('#080F1B'), outlineWidth: 3,
          horizontalOrigin: HorizontalOrigin.LEFT, pixelOffset: new Cartesian2(14, -14) },
      });
      endMarkers.push(viewer.entities.add({ parent: trajectoryRoot, show: showTrajectory, position: positions.at(-1)!,
        point: { pixelSize: 5, color: Color.fromCssColorString('#9BB1CA'), outlineColor: Color.fromCssColorString('#080F1B'), outlineWidth: 1 },
      }));
      }
      const snapshot = snapshots[0];
      observerLayer.setOrbitPositions(trajectoryPositions);
      if (!snapshot) { report({ type: 'trajectory-loaded', keys: [], sampleCount: 0, requestId }); return; }
      const date = new Intl.DateTimeFormat('en-GB', { day: 'numeric', month: 'short', year: 'numeric', timeZone: 'UTC' });
      const time = new Intl.DateTimeFormat('en-GB', { hour: '2-digit', minute: '2-digit', hourCycle: 'h23', timeZone: 'UTC' });
      const start = new Date(snapshot.epochUtc);
      const end = new Date(snapshot.trajectory.samples.at(-1)!.epochUtc);
      const heading = document.createElement('strong');
      heading.textContent = snapshots.length === 1 ? `${snapshot.target.name} / Reference orbit` : `${snapshots.length} selected satellites / Reference orbits`;
      const period = document.createElement('span');
      period.className = 'snapshot-period';
      period.textContent = `Reference epoch: ${date.format(start)} · ${time.format(start)} UTC`;
      period.title = `CPF trails: ${snapshot.epochUtc} to ${snapshot.trajectory.samples.at(-1)!.epochUtc}`;
      const caption = document.createElement('span');
      caption.className = 'snapshot-caption';
      caption.textContent = 'Closed loops: approximate two-body geometry';
      const legend = document.createElement('span');
      legend.className = 'snapshot-caption';
      for (const target of snapshots) {
        const entry = document.createElement('span');
        entry.textContent = `● ${target.target.name}  `;
        entry.style.color = palette[target.target.key] ?? '#E4B76F';
        legend.append(entry);
      }
      previewPanel.replaceChildren(heading, period, caption, legend);
      previewPanel.hidden = false;
      viewer.scene.requestRender();
      report({ type: 'trajectory-loaded', key: snapshot.target.key, keys: snapshots.map(snapshot => snapshot.target.key), sampleCount: snapshots.reduce((total, snapshot) => total + snapshot.trajectory.samples.length, 0), requestId });
    } catch (error) {
      clearTrajectory();
      report({ type: 'trajectory-error', message: error instanceof Error ? error.message : String(error), requestId });
    }
  }
  function applyLiveFrame(value: unknown) {
    liveMode = true;
    try {
      if (value && typeof value === 'object' && 'error' in value) throw new Error(String(value.error));
      const frame = validateLiveFrame(value);
      if (!observerLayer.matchesObserver(frame.observer)) return;
      if (frame.targets.length !== liveKeys.size || frame.targets.some(target => !liveKeys.has(target.key))) return;
      if (frame.snapshots) loadTrajectory(frame.snapshots);
      let ready = 0;
      const activeBounds: Cartesian3[] = [];
      for (const target of frame.targets) {
        let root = viewer.entities.getById(`prediction-${target.key}`);
        if (target.state !== 'ready') { if (root) root.show = false; continue; }
        liveExpiry.set(target.key, Date.parse(target.cpf!.usableEndUtc));
        if (Date.now() > Date.parse(target.cpf!.usableEndUtc)) { if (root) root.show = false; continue; }
        ready++;
        activeBounds.push(...(orbitBounds.get(target.key) ?? [new Cartesian3(...target.positionEcefM!)]));
        if (!root) { root = viewer.entities.add({ id: `prediction-${target.key}`, name: target.name }); trajectoryRoots.push(root); }
        root.show = true;
        const color = Color.fromCssColorString(palette[target.key] ?? '#E4B76F');
        let marker = viewer.entities.getById(`marker-${target.key}`);
        if (!marker) marker = viewer.entities.add({ id: `marker-${target.key}`, parent: root,
          point: { pixelSize: 8, color, outlineColor: Color.BLACK, outlineWidth: 2 },
          label: { text: target.name, font: '13px sans-serif', fillColor: color, style: LabelStyle.FILL_AND_OUTLINE,
            outlineColor: Color.fromCssColorString('#080F1B'), outlineWidth: 3, horizontalOrigin: HorizontalOrigin.LEFT, pixelOffset: new Cartesian2(14, -14) },
        });
        marker.position = new ConstantPositionProperty(new Cartesian3(...target.positionEcefM!));
        if (marker.label) marker.label.text = new ConstantProperty(`${target.name} · ${target.look!.elevationDeg.toFixed(1)}°`);
        if (marker.point) marker.point.color = new ConstantProperty(target.look!.aboveThreshold ? Color.fromCssColorString('#88E5A5') : Color.fromCssColorString('#8490A2'));
        if (marker.label) marker.label.fillColor = new ConstantProperty(target.look!.aboveThreshold ? Color.fromCssColorString('#88E5A5') : color.withAlpha(0.7));
      }
      observerLayer.setOrbitPositions(activeBounds);
      liveEpochMs = Date.parse(frame.epochUtc);
      liveReceivedMs = performance.now();
      const heading = document.createElement('strong');
      heading.textContent = `LIVE CPF predictions / ${ready} of ${frame.targets.length} ready`;
      const epoch = document.createElement('span');
      epoch.className = 'snapshot-period';
      epoch.textContent = `${frame.epochUtc.replace('T', ' ').replace('Z', ' UTC')} · 1 s updates`;
      const caption = document.createElement('span');
      caption.className = 'snapshot-caption';
      caption.textContent = `Green marker: ≥ ${frame.observer.minElevationDeg}° · Closed loops: approximate reference geometry`;
      previewPanel.replaceChildren(heading, epoch, caption);
      previewPanel.dataset.liveEpoch = frame.epochUtc;
      previewPanel.hidden = false;
      viewer.scene.requestRender();
      report({ type: 'live-frame-rendered', epochUtc: frame.epochUtc, readyCount: ready });
    } catch (error) {
      const message = error instanceof Error ? error.message : String(error);
      liveUnavailable(`Live state unavailable · ${message}`);
      report({ type: 'live-error', message });
    }
  }
  window.chrome?.webview?.addEventListener('message', event => {
    switch (event.data?.command) {
      case 'begin-live': liveMode = true; liveKeys = new Set(event.data.keys); liveExpiry.clear(); clearTrajectory(); liveUnavailable(liveKeys.size ? 'Loading current CPF predictions…' : 'Select satellites for live CPF predictions'); break;
      case 'live-frame': applyLiveFrame(event.data.frame); break;
      case 'global': globalView(); break;
      case 'set-observer':
        try {
          const sameSite = observerLayer.matchesObserver({ ...event.data.observer, minElevationDeg: event.data.observer.minElevationDeg ?? 0 });
          const observer = observerLayer.apply(event.data.observer);
          if (liveMode && !sameSite) liveUnavailable('Updating observer and minimum-elevation visibility…');
          report({ type: 'observer-updated', observer });
        } catch (error) { report({ type: 'observer-error', message: error instanceof Error ? error.message : String(error) }); }
        break;
      case 'observer': observerLayer.focus(); break;
      case 'observer-side': observerLayer.focus(true); break;
      case 'grid-on': grid.show = true; viewer.scene.requestRender(); break;
      case 'grid-off': grid.show = false; viewer.scene.requestRender(); break;
      case 'load-trajectory': loadTrajectory(event.data.snapshot); break;
      case 'set-trajectories': loadTrajectory(event.data.snapshots, event.data.requestId); break;
      case 'clear-trajectory': clearTrajectory(); break;
      case 'trajectory-view': frameTrajectory(); break;
      case 'trajectory-on': showTrajectory = true; for (const line of [...trajectoryLines, ...endMarkers]) line.show = true; viewer.scene.requestRender(); break;
      case 'trajectory-off': showTrajectory = false; for (const line of [...trajectoryLines, ...endMarkers]) line.show = false; viewer.scene.requestRender(); break;
      case 'reference-on': showReference = true; for (const line of referenceLines) line.show = true; viewer.scene.requestRender(); break;
      case 'reference-off': showReference = false; for (const line of referenceLines) line.show = false; viewer.scene.requestRender(); break;
    }
  });
  viewer.scene.postRender.addEventListener(function ready() {
    viewer.scene.postRender.removeEventListener(ready);
    report({ type: 'ready' });
  });
  viewer.scene.requestRender();
}
start().catch(fail);
