# Offline node panel

Actual BaklavaJS 2.8.1 renderer/core, Vue 3.5.22, classic Baklava theme.
`npm ci` then `npm run build` generates the single offline firmware HTML asset.
From the project root, `node web-editor/test.cjs` exercises the actual graph and
configuration mapping. Node.js 18+ is recommended by the esbuild package.

The framework handles ports, curved paths, mouse interaction and node movement.
Wheel zoom is disabled. The render hook in src/layout.js aligns input/output
ports on shared 24 px rows and quantizes tile dimensions to the fixed grid.
Application code maps connections to the existing eight hub slots / eight trains.
Edits remain a draft until saved to `/api/config` (STOP and reboot).
LED changes and STOP actions apply immediately. Motor tests require saved settings.

`build.cjs` has asserted, pinned renderer seams for the reactive offline path class,
orthogonal routing and disabled wheel zoom. Review them before upgrading BaklavaJS.
No npm packages, CDN requests or external fonts are needed on the ESP32 at runtime.
Third-party licenses accompany both the source and generated HTML.
