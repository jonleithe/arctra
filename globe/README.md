# Arctra globe frontend

This is the shared CesiumJS/TypeScript renderer adapted from ArctraWin commit
`4159d2d`. In a Windows WebView2 host it retains the original message bridge.
In a normal browser, `browser-host.ts` supplies the catalog/sidebar and talks
to Arctra's loopback-only Linux host.

The renderer validates versioned JSON contracts before displaying state. It
does not parse CPF, interpolate positions, calculate look angles, or extend
prediction coverage. Those operations remain in the C++ worker.

The standalone browser requests a fresh station-classified, one-revolution
orbit when the selected targets, observer, or minimum elevation change. The
worker estimates the period from a centred CPF state difference, removes
nominal Earth rotation, and freezes the display axes at the current epoch.
Thin grey is below threshold; coloured overlays are at or above threshold.
Threshold decisions and 10 ms crossing refinement use the original Earth-fixed
CPF positions. The frozen orbit is contextual because CPF alone supplies no
UT1–UTC or polar motion. The browser performs no orbital calculation.

Build and test from this directory with a supported Node.js/npm installation:

```bash
npm ci
npm run build
npx playwright install chromium  # first Linux test run only
npm test
```

`public/cesium/`, `dist/`, test results, and `node_modules/` are generated and
ignored. `public/catalog.csv` is the checked-in catalog used by the standalone
sidebar. The trajectory fixtures retain the reproducible ArctraWin test data;
they are not used as live predictions.
