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

  // About modal surfaces the version + source URL — pulled in via Vite's
  // `define` so build / CI can override without touching JS. The defaults
  // here track the current MVP; CI overrides via `--define ...` if needed.
  define: {
    __GENVST_VERSION__:    JSON.stringify("0.2.1"),
    __GENVST_SOURCE_URL__: JSON.stringify("https://github.com/binbuf/gen-vst"),
  },

  build: {
    outDir: "dist",
    emptyOutDir: true,
    target: "es2020",
    assetsInlineLimit: 0, // keep fonts as real files — never inline as data URIs
    sourcemap: true,      // stack traces from main.js's init() try/catch land in
                          // ui/src/* file/line coordinates instead of the minified
                          // bundle (~50 KB of .map files alongside the JS chunks).
    rollupOptions: {
      // Two entry points (mvp2/04-widget-library.md):
      //   - main    -> index.html      (the v2 production UI — empty chassis
      //                                 in Task 04; Tasks 05-07 fill in the
      //                                 per-mode panels)
      //   - gallery -> gallery.html    (the developer widget showroom, bound
      //                                 against the gallery_* scratch params)
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
