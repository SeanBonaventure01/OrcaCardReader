import preact from '@preact/preset-vite';
import { defineConfig } from 'vitest/config';

// `npm run dev` proxies the API to orca-sim (make sim) on :8080.
export default defineConfig({
  plugins: [preact()],
  build: {
    outDir: 'dist',
    assetsInlineLimit: 0,
    // One JS + one CSS file keeps the firmware's static file table small.
    rollupOptions: { output: { manualChunks: undefined, entryFileNames: 'app.js', assetFileNames: '[name][extname]' } },
  },
  server: {
    proxy: {
      '/api': 'http://localhost:8080',
      '/ws': { target: 'ws://localhost:8080', ws: true },
    },
  },
  test: {
    environment: 'jsdom',
    include: ['tests/**/*.test.{ts,tsx}'],
  },
});
