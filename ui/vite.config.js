import { defineConfig } from "vite";
import { fileURLToPath } from "node:url";
import { dirname, resolve } from "node:path";

const here = dirname(fileURLToPath(import.meta.url));

// The bitmap/segment fonts live in extern/fonts/ — shared with the rest of the
// repo, not duplicated inside ui/. Point Vite's publicDir there: its contents
// are served at the web root in dev (npm run dev) and copied verbatim into
// dist/ on build (06-build-system.md, 05-ui-ux.md "Fonts").
export default defineConfig({
  root: here,
  publicDir: resolve(here, "../extern/fonts"),

  // Relative asset URLs so the bundle works under the editor's resource-provider
  // root (https://juce.backend/...) without depending on the host path.
  base: "./",

  build: {
    outDir: "dist",
    emptyOutDir: true,
    target: "es2020",
    assetsInlineLimit: 0, // keep fonts as real files — never inline as data URIs
    rollupOptions: {
      // Multi-page setup: index.html is the production UI, gallery.html is a
      // dev-only widget catalog (Task 10). Both build into dist/ — the
      // gallery is reachable in dev with `npm run dev` open /gallery.html.
      input: {
        main:    resolve(here, "index.html"),
        gallery: resolve(here, "gallery.html"),
      },
    },
  },

  server: {
    port: 5173,
    strictPort: true, // GENVST_DEV_SERVER expects exactly localhost:5173
    fs: {
      allow: [resolve(here, "..")], // permit serving fonts from ../extern/fonts
    },
  },
});
