# Task 03 — WebView shell, design system & static chassis

> **Milestone:** E2E #3 — the plugin has a real UI.
> **Depends on:** Task 01, Task 02.
> **Design references:** `docs/design/05-ui-ux.md` (primary),
> `docs/design/06-build-system.md` (*Web UI Build*, *src/CMakeLists.txt*),
> `docs/genny-ui.md`, `docs/design/08-ui-views.md` (view 1 layout),
> ADR-0001, ADR-0007, ADR-0015.

## Objective

Replace the native placeholder editor with a **JUCE 8 WebView** hosting an
HTML/CSS/JS app, prove the full C++↔JS contract with one working control, and
establish the pixel-art design system + static chassis layout so later UI tasks
have a foundation. This retires the WebView integration risk.

## Context & key constraints

- The UI is an HTML/CSS/JS app rendered in `juce::WebBrowserComponent`
  (ADR-0001). Frontend stack: **vanilla JS + Canvas 2D, bundled by Vite**, npm
  package manager, no framework.
- The JUCE 8 `WebViewPluginDemo` example is the reference for relays, native
  functions, and the resource provider — follow its patterns.
- **Build wiring** (`06-build-system.md` *Web UI Build* + *src/CMakeLists.txt*):
  a CMake `add_custom_command` runs `npm ci` + `npm run build` and zips
  `ui/dist` into `genvst-ui.zip`; `juce_add_binary_data(GenVstWebData ...)`
  embeds it; the editor's resource provider serves files out of the zip in
  release builds. `find_program(NPM_EXECUTABLE NAMES npm npm.cmd REQUIRED)` —
  the Windows launcher is `npm.cmd`.
- **Dev mode:** the `GENVST_DEV_SERVER` CMake option compiles in a flag that
  makes the editor load `http://localhost:5173` (Vite dev server) instead of the
  embedded bundle, for hot reload.
- The `WebBrowserComponent` is configured with `withNativeIntegrationEnabled()`,
  `withResourceProvider(...)`, and the parameter relays via `withOptionsFrom(...)`
  — see `05-ui-ux.md` *Editor host*.
- **Parameter binding:** a knob binds through a `WebSliderRelay` +
  `WebSliderParameterAttachment` pair; the relay name **equals** the `apvts`
  parameter ID. The binding is two-way (`05-ui-ux.md` *Parameter binding*).
- **Pixel-art rules are binding** (`05-ui-ux.md` *Pixel-Art Style Rules*):
  1× grid, `image-rendering: pixelated`, `ctx.imageSmoothingEnabled = false`,
  **no `border-radius`**, hard-border bevels, no gradients (ordered dithering
  instead), 8px base grid.
- **Palette & fonts:** colors come from the `genny-ui.md` palette table as named
  CSS custom properties — never hand-typed hex per component. Fonts: Press Start
  2P and the 7-segment face are already in `extern/fonts/`; the Vite build pulls
  them into `ui/dist` (`05-ui-ux.md` *Fonts*).
- Resource provider must return correct `Content-Type` for every asset
  including `.woff2`/`.ttf` — WebKit backends reject wrong MIME types
  (ADR-0015).
- Window stays fixed 960×560 (ADR-0007).
- `EDITOR_WANTS_KEYBOARD_FOCUS TRUE` (already set in Task 01) — the HTML UI has
  inputs.

## Scope

- `ui/` Vite project: `package.json`, `package-lock.json`, `vite.config.js`,
  `index.html`, `src/` (entry JS + CSS).
- The CMake web-build pipeline + `GenVstWebData` binary-data target +
  `GenVstWebBundle` dependency, added to `src/CMakeLists.txt` per the design.
- `GENVST_DEV_SERVER` option + compile definition.
- The editor rewritten to host `juce::WebBrowserComponent` (native integration,
  resource provider, dev-server switch), replacing the Task 01 placeholder.
- **Design-system CSS:** the `genny-ui.md` palette as CSS custom properties; the
  pixel-art base rules; `@font-face` for both fonts.
- **Static chassis layout:** the four-region 960×560 frame from `genny-ui.md` /
  `08-ui-views.md` view 1 — header bar, left/center/right columns, bottom
  operator-panel strip — as static HTML/CSS (dark chassis, beveled insets,
  region placeholders). No live widgets yet.
- **One working control:** an HTML knob (Canvas-drawn is fine, or a styled
  `<input type=range>` for this task) bound via `WebSliderRelay` /
  `WebSliderParameterAttachment` to the `master_gain` parameter from Task 02.
- A `uiReady` event fired by the page once mounted; the editor listens for it.

## Out of scope

- The full Canvas widget library (knob, slider, led-readout, …) → Task 10.
- The live FM view contents → Task 11. SQ/D views & modals → Tasks 13.
- Telemetry push (oscilloscope/VU) → Task 12.
- Native file drag-and-drop, native functions beyond what the shell needs →
  Tasks 11/14.
- The WebView fallback panel & HiDPI scaling → Task 17.

## Implementation steps

1. Scaffold the `ui/` Vite project (vanilla template). Commit
   `package-lock.json`. Configure `vite.config.js` so fonts from
   `extern/fonts/` are pulled into the build output.
2. Add the CMake web-build `add_custom_command` + `GenVstWebBundle` target +
   `juce_add_binary_data(GenVstWebData ...)` and `add_dependencies`, per
   `06-build-system.md`. Link `GenVstWebData` into the plugin.
3. Add the `GENVST_DEV_SERVER` option and its compile definition.
4. Rewrite `PluginEditor` to own a `juce::WebBrowserComponent` sized 960×560,
   configured with native integration, the resource provider (serving from the
   embedded zip; correct MIME types), and — under `GENVST_DEV_SERVER` — loading
   `http://localhost:5173`.
5. Author the design-system CSS: palette custom properties, pixel-art base
   rules, `@font-face` declarations.
6. Build the static chassis layout (HTML/CSS) for the four regions.
7. Register a `master_gain` `WebSliderRelay` on the `WebBrowserComponent`
   options and a `WebSliderParameterAttachment` to the `apvts` parameter; add
   the bound knob to the header or left column.
8. Fire `uiReady` from JS on mount; have the editor log/observe it.

## Deliverables

`ui/package.json`, `ui/package-lock.json`, `ui/vite.config.js`, `ui/index.html`,
`ui/src/*` (entry, design-system CSS, chassis layout, the bound knob),
updates to `src/PluginEditor.{h,cpp}`, `src/CMakeLists.txt`, root `CMakeLists.txt`
(the `GENVST_DEV_SERVER` option if not added in Task 01).

## Verification

1. Release-style build: `cmake --preset windows-debug` (default,
   `GENVST_DEV_SERVER=OFF`) then build — the web bundle builds, `genvst-ui.zip`
   is embedded, no `npm` errors.
2. Launch the Standalone — the editor shows the **pixel-art Genny chassis**
   (dark frame, four regions, beveled insets), not the old placeholder. Fonts
   render (no fallback/system font). No `border-radius`, no blur.
3. The bound knob is visible. Dragging it **audibly changes the master volume**
   while a note plays, and the DAW automation lane for `master_gain` moves with
   it (two-way binding) — verify in a DAW.
4. Moving `master_gain` from the DAW automation lane moves the knob in the UI.
5. `pluginval --strictness-level 8` still passes.
6. Dev workflow: reconfigure with `-DGENVST_DEV_SERVER=ON`, run `npm run dev` in
   `ui/`, launch the Standalone — it loads from `localhost:5173`; edit a CSS
   color and confirm the running plugin hot-reloads.
7. The editor opens and closes repeatedly without leaking or crashing; audio
   keeps running while the editor is closed.

## Done when

- [ ] CMake builds the Vite bundle and embeds it; no manual `npm` step needed.
- [ ] The editor hosts a WebView showing the static pixel-art chassis with
      correct palette and fonts.
- [ ] One knob is two-way-bound to `master_gain` and audibly works.
- [ ] `GENVST_DEV_SERVER=ON` hot-reload workflow works.
- [ ] `pluginval` passes; editor open/close is clean.
