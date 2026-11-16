import { defineConfig } from '@playwright/test';

export default defineConfig({
  testDir: './tests',
  use: { ...(process.platform === 'win32' ? { channel: 'msedge' } : { browserName: 'chromium' as const }),
    viewport: { width: 1100, height: 800 } },
  webServer: { command: 'node node_modules/vite/bin/vite.js preview --host 127.0.0.1 --port 4173 --strictPort', url: 'http://127.0.0.1:4173', reuseExistingServer: false },
});
