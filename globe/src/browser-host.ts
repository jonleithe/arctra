export {};

interface Target {
  key: string;
  name: string;
  noradId: number;
  ilrsId: number;
}

type HostMessage = { command: string; [key: string]: unknown };
type RendererStatus = { type?: string; [key: string]: unknown };

declare global {
  interface Window {
    chrome?: { webview?: {
      postMessage: (message: unknown) => void;
      addEventListener: (type: 'message', listener: (event: MessageEvent) => void) => void;
    } };
  }
}

const nativeBridge = window.chrome?.webview;
if (nativeBridge) {
  await import('./main');
} else {
  const listeners: Array<(event: MessageEvent) => void> = [];
  let onRendererStatus = (_message: RendererStatus) => {};
  window.chrome = { webview: {
    addEventListener: (_type, listener) => listeners.push(listener),
    postMessage: message => onRendererStatus(message as RendererStatus),
  } };
  const send = (message: HostMessage) => {
    const event = new MessageEvent('message', { data: message });
    for (const listener of listeners) listener(event);
  };

  document.body.classList.add('standalone');
  const shell = document.createElement('aside');
  shell.id = 'standalone-controls';
  shell.innerHTML = `
    <header><span class="wordmark">ARCTRA</span><span> / EARTH VIEW</span></header>
    <section>
      <h1>Satellites</h1>
      <p class="muted">EDC active-target catalog · geometric CPF predictions</p>
      <input id="target-search" type="search" placeholder="Name, CPF key, NORAD or ILRS ID" aria-label="Search satellites">
      <div id="target-count" class="muted"></div>
      <div id="target-list"></div>
    </section>
    <section class="settings">
      <h2>Observer</h2>
      <label>Name <input id="observer-name" value="Ny-Ålesund"></label>
      <div class="coordinates">
        <label>Latitude ° <input id="observer-lat" type="number" step="0.0001" value="78.923"></label>
        <label>Longitude ° <input id="observer-lon" type="number" step="0.0001" value="11.923"></label>
      </div>
      <div class="coordinates">
        <label>Ellipsoid height m <input id="observer-height" type="number" step="1" value="50"></label>
        <label>Minimum elevation ° <input id="observer-elevation" type="number" step="0.5" value="10"></label>
      </div>
      <div class="actions"><button id="global-view">Global view</button><button id="observer-view">Observer view</button></div>
      <label class="check"><input id="trajectory-toggle" type="checkbox" checked> CPF trajectory and visibility</label>
      <label class="check"><input id="grid-toggle" type="checkbox"> Latitude / longitude grid</label>
    </section>
    <footer><strong id="live-badge">LIVE CPF · STARTING</strong><span id="live-status">Preparing globe…</span></footer>`;
  document.body.append(shell);

  const byId = <T extends HTMLElement>(id: string) => document.querySelector<T>(`#${id}`)!;
  const list = byId<HTMLDivElement>('target-list');
  const count = byId<HTMLDivElement>('target-count');
  const search = byId<HTMLInputElement>('target-search');
  const status = byId<HTMLSpanElement>('live-status');
  const badge = byId<HTMLElement>('live-badge');
  const startupResponse = await fetch('/startup.json', { cache: 'no-store' });
  if (!startupResponse.ok) throw new Error('Globe startup configuration is unavailable.');
  const startup = await startupResponse.json() as {
    observer: { name: string; latitudeDeg: number; longitudeDeg: number;
      ellipsoidalHeightM: number; minElevationDeg: number } | null;
    initialTargets: string[]; overrideSavedTargets: boolean;
  };
  if (!Array.isArray(startup.initialTargets) || !startup.initialTargets.every(key => typeof key === 'string') ||
      typeof startup.overrideSavedTargets !== 'boolean') throw new Error('Invalid globe startup configuration.');
  if (startup.observer) {
    const values = [startup.observer.latitudeDeg, startup.observer.longitudeDeg,
      startup.observer.ellipsoidalHeightM, startup.observer.minElevationDeg];
    if (typeof startup.observer.name !== 'string' || !startup.observer.name || !values.every(Number.isFinite))
      throw new Error('Invalid startup observatory.');
    byId<HTMLInputElement>('observer-name').value = startup.observer.name;
    byId<HTMLInputElement>('observer-lat').value = String(startup.observer.latitudeDeg);
    byId<HTMLInputElement>('observer-lon').value = String(startup.observer.longitudeDeg);
    byId<HTMLInputElement>('observer-height').value = String(startup.observer.ellipsoidalHeightM);
    byId<HTMLInputElement>('observer-elevation').value = String(startup.observer.minElevationDeg);
  }
  let targets: Target[] = [];
  const saved = JSON.parse(localStorage.getItem('arctra-selected-targets-v1') ?? '[]');
  const selected = new Set<string>(startup.overrideSavedTargets
    ? startup.initialTargets : (Array.isArray(saved) ? saved : []));
  let rendererReady = false;
  let polling = false;
  let trajectoriesNeeded = true;
  let liveGeneration = 0;

  const observer = () => ({
    name: byId<HTMLInputElement>('observer-name').value.trim() || 'Observer',
    latitudeDeg: Number(byId<HTMLInputElement>('observer-lat').value),
    longitudeDeg: Number(byId<HTMLInputElement>('observer-lon').value),
    ellipsoidalHeightM: Number(byId<HTMLInputElement>('observer-height').value),
    minElevationDeg: Number(byId<HTMLInputElement>('observer-elevation').value),
    planeWidthKm: 12000, showPlane: true, showElevationCone: true,
  });

  function renderTargets() {
    const query = search.value.trim().toLowerCase().replace(/[-_ ]/g, '');
    const visible = targets.filter(target => !query ||
      `${target.key}${target.name}${target.noradId}${target.ilrsId}`.toLowerCase().replace(/[-_ ]/g, '').includes(query));
    list.replaceChildren(...visible.map(target => {
      const label = document.createElement('label');
      label.className = 'target';
      const checkbox = document.createElement('input');
      checkbox.type = 'checkbox'; checkbox.checked = selected.has(target.key);
      checkbox.addEventListener('change', () => {
        checkbox.checked ? selected.add(target.key) : selected.delete(target.key);
        localStorage.setItem('arctra-selected-targets-v1', JSON.stringify([...selected]));
        beginLive();
      });
      const text = document.createElement('span');
      text.innerHTML = `<strong>${target.name}</strong><small>${target.key} · NORAD ${target.noradId} · ILRS ${target.ilrsId}</small>`;
      label.append(checkbox, text);
      return label;
    }));
    count.textContent = `${visible.length} shown · ${selected.size} selected`;
  }

  function beginLive() {
    if (!rendererReady) return;
    liveGeneration++;
    const keys = targets.filter(target => selected.has(target.key)).map(target => target.key);
    send({ command: 'begin-live', keys });
    send({ command: 'set-observer', observer: observer() });
    trajectoriesNeeded = true;
    count.textContent = `${targets.length} targets · ${keys.length} selected`;
  }

  async function poll() {
    if (polling || !rendererReady) return;
    polling = true;
    const requestGeneration = liveGeneration;
    const chosen = targets.filter(target => selected.has(target.key));
    try {
      const response = await fetch('/api/live', { method: 'POST', headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ schemaVersion: 1, observer: observer(), targets: chosen,
          includeTrajectories: trajectoriesNeeded }) });
      const frame = await response.json();
      if (!response.ok || frame.ok !== true) throw new Error(frame.error ?? `HTTP ${response.status}`);
      send({ command: 'live-frame', frame });
      if (requestGeneration === liveGeneration && Array.isArray(frame.snapshots)) trajectoriesNeeded = false;
      const ready = frame.targets.filter((target: { state: string }) => target.state === 'ready').length;
      badge.textContent = `LIVE CPF · ${ready}/${chosen.length}`;
      status.textContent = `${frame.epochUtc.replace('T', ' ').replace('Z', ' UTC')} · instantaneous geometric CPF`;
    } catch (error) {
      badge.textContent = 'LIVE CPF · UNAVAILABLE';
      status.textContent = error instanceof Error ? error.message : String(error);
    } finally { polling = false; }
  }

  onRendererStatus = message => {
    if (message.type === 'ready') {
      rendererReady = true;
      send({ command: byId<HTMLInputElement>('trajectory-toggle').checked ? 'trajectory-on' : 'trajectory-off' });
      beginLive(); void poll();
    }
    if (message.type === 'live-error') status.textContent = String(message.message ?? 'Renderer rejected live state');
  };
  search.addEventListener('input', renderTargets);
  for (const id of ['observer-name', 'observer-lat', 'observer-lon', 'observer-height', 'observer-elevation'])
    byId<HTMLInputElement>(id).addEventListener('change', beginLive);
  byId<HTMLButtonElement>('global-view').addEventListener('click', () => send({ command: 'global' }));
  byId<HTMLButtonElement>('observer-view').addEventListener('click', () => send({ command: 'observer' }));
  byId<HTMLInputElement>('trajectory-toggle').addEventListener('change', event =>
    send({ command: (event.currentTarget as HTMLInputElement).checked ? 'trajectory-on' : 'trajectory-off' }));
  byId<HTMLInputElement>('grid-toggle').addEventListener('change', event =>
    send({ command: (event.currentTarget as HTMLInputElement).checked ? 'grid-on' : 'grid-off' }));

  const catalogResponse = await fetch('/catalog.csv');
  if (!catalogResponse.ok) throw new Error('Satellite catalog is unavailable.');
  const rows = (await catalogResponse.text()).split(/\r?\n/).filter(line => line && !line.startsWith('#'));
  targets = rows.slice(1).map(line => {
    const [key, name, ilrsId, noradId] = line.split(',');
    return { key, name, ilrsId: Number(ilrsId), noradId: Number(noradId) };
  }).filter(target => target.noradId > 0);
  renderTargets();
  await import('./main');
  setInterval(() => void poll(), 1000);
}
