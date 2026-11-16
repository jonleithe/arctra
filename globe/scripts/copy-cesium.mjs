import { cp, mkdir } from 'node:fs/promises';
const source = new URL('../node_modules/cesium/Build/Cesium/', import.meta.url);
const target = new URL('../public/cesium/', import.meta.url);
await mkdir(target, { recursive: true });
for (const name of ['Assets', 'ThirdParty', 'Workers', 'Widgets']) {
  await cp(new URL(name, source), new URL(name, target), { recursive: true });
}
