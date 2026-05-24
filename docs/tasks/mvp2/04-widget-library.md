# Task 04 — v2 widget library & gallery

> **Milestone:** Widget library — every v2 widget exists as a real JS
> module bound through the relay layer; a gallery page exercises each.
> **Depends on:** Task 03.
> **Design references:** `docs/design/05-ui-ux.md` (primary —
> *Component Inventory*, *Parameter binding*),
> `docs/design/09-visual-spec.md` (palette tokens, CSS recipes per
> widget), `docs/design/08-ui-views.md` view 9 (fallback panel — the
> placeholder editor is replaced by the WebView in this task), ADR-0001,
> ADR-0022, ADR-0015.

## Objective

Reintroduce the WebView editor and build the **v2 widget library** —
every reusable HTML/CSS/Canvas component from `05-ui-ux.md` *Component
Inventory* — with two-way binding to apvts parameter relays. A dev-only
**gallery page** exercises every widget against scratch parameters so
each can be developed and verified in isolation before the per-mode
panels are assembled in Tasks 05–07.

After this task: the plugin opens the WebView (or the fallback panel on
WebView2 failure); the WebView shows the gallery page when the host
selects it; the placeholder editor from Task 02 is gone.

## Context & key constraints

- **The mockup pages from Task 01 are deleted at the end of this task**
  (their CSS recipes are subsumed by the real widget library files).
  `design-system.css` survives unchanged — every widget references its
  palette tokens. If a recipe deviates from the mockup, the **mockup
  was wrong**; cite the visual-spec recipe and fix the widget.
- **Widgets list** (`05-ui-ux.md` *Component Inventory*): `knob`,
  `button`, `stepper`, `lcd-readout`, `toggle-switch`, `slider`,
  `algo-grid`, `algorithm-mini`, `envelope-curve`, `note-on-led`, `level-meter`,
  `decimator-knob` (a variant of `knob`), `patch-name-lcd` (a variant
  of `lcd-readout`), `op-badge`, `notification-toast`.
- **Rendering split** (`05-ui-ux.md` *Asset Strategy*):
  - **HTML + CSS** — `button`, `toggle-switch`, `slider`, `op-badge`,
    `note-on-led` (CSS animation on apvts-bound state),
    `notification-toast`.
  - **Canvas 2D** — `knob` (body via CSS, indicator arc via canvas),
    `decimator-knob` (same), `lcd-readout` (canvas drawn glowing
    text), `patch-name-lcd` (canvas), `algorithm-mini` (canvas),
    `envelope-curve` (canvas), `level-meter` (canvas), `stepper`
    (CSS frame + canvas LCD digits).
  - **No SVG, no WebGL** (`05-ui-ux.md`).
- **Binding layer** (`05-ui-ux.md` *Parameter binding*): each widget
  binds to its relay in one call:
  - `bindSlider(name)` — knobs, sliders, decimator-knob, master volume.
  - `bindToggle(name)` — toggles, LED rockers, op-badge active state
    (no — op-badge.active is a JS-local UI state, not apvts).
  - `bindCombo(name)` — mode selector, FREQ CTRL MODE pill, noise
    type / rate, SSG-EG combo.
  - Relays + attachments wired through `WebSliderRelay /
    WebSliderParameterAttachment`, `WebToggleButtonRelay /
    WebToggleButtonParameterAttachment`, `WebComboBoxRelay /
    WebComboBoxParameterAttachment`. The **relay name equals the apvts
    parameter ID**.
- **Knob interaction** (`08-ui-views.md` widget references): vertical
  click-drag (up = increase), `Shift` = fine drag, double-click =
  reset to default, scroll-wheel increment. ~270° sweep, rest at
  7 o'clock.
- **Stepper interaction**: ▲/▼ click increments; click-and-hold
  repeats (after ~300 ms, ~10 Hz); scroll-wheel = ±1.
- **Algorithm picker is an `algo-grid`** (`08-ui-views.md` view 2,
  `09-visual-spec.md` *Algorithm picker — `algo-grid`*): 8 visible
  numbered buttons in a 2 × 4 CSS grid; the selected one carries
  `.is-active`; click selects, no popover. A separate larger
  `algorithm-mini` (canvas, ~112 px) renders the selected algorithm's
  topology read-only. Bound to apvts param `algorithm` (0..7).
- **Op-badge active state** is local to the FM panel (Task 05) — it
  selects which operator the envelope-curve widget tracks. The widget
  exposes a `.setActive(bool)` method but does not bind to apvts.
- **Envelope-curve widget** draws the ADSR polyline plus segment labels
  (`AR`, `DR`, `SL`, `SR`, `RR`) at each segment's midpoint, plus
  dashed vertical `KEY ON` / `KEY OFF` markers. Recipe in
  `09-visual-spec.md` *Envelope curve (canvas)*. The label positions
  recompute whenever any of the five envelope-knob values change.
- **NOTE ON LED + text label** (`note-on` cluster, `09-visual-spec.md`
  *NOTE ON LED*): pairs a 16 px round `.note-on-led` with a stacked
  `NOTE ON` text caption inside a `.note-on` wrapper. The LED binds to
  a telemetry boolean (not an apvts param): reads the same `noteOn`
  value the C++ telemetry exposes through the `meterData` event (Task
  03 already pushes it). The text caption is static.
- **Level meters** subscribe to the `meterData` event (`05-ui-ux.md`
  *C++ → JS telemetry push*) — `peakL`, `peakR`. ~30 Hz redraw is fine.
- **WebView host** is re-established now (the fallback editor from Task
  02 is replaced):
  - `juce::WebBrowserComponent` sized 1200×560.
  - `withNativeIntegrationEnabled()`,
    `withResourceProvider(...)` for the embedded Vite bundle, relays
    registered via `withOptionsFrom(...)`, native functions registered
    via `withNativeFunction(...)`.
  - Dev-server compile flag `GENVST_DEV_SERVER` selects
    `http://localhost:5173` vs the embedded bundle (`06-build-system.md`).
  - Resource provider returns correct MIME types for every served
    asset, including `.woff2` (WebKit rejects wrong MIME).
- **Fallback panel** (`08-ui-views.md` view 9) — the editor falls back
  to the placeholder from Task 02 if the WebView fails to initialise.
  Retry button calls `tryInitWebView()` again.
- **Gallery page** survives in dev builds and the production bundle
  (multi-page Vite entry, as in v1). Reachable at
  `http://localhost:5173/gallery.html` and `dist/gallery.html`.
- **Scratch params for the gallery** — add a handful of off-the-shelf
  apvts params with names like `gallery_knob_a`, `gallery_toggle_a`,
  etc. behind a `#ifdef GENVST_DEV_SERVER` (or always — the storage
  cost is trivial). The gallery binds to those.

## Scope

- Re-introduce the WebView editor (replaces the Task 02 fallback
  placeholder; the fallback panel is the failure-mode editor).
- `juce_add_binary_data(GenVstWebData …)` + `GenVstWebBundle` custom
  target return to `src/CMakeLists.txt` (the Task 02 deletions are
  reversed).
- `ui/index.html` — the v2 production entry. Imports the gallery's
  widget modules, mounts a single host that renders nothing (the per-
  mode panels arrive in Tasks 05–07). For now this page can render an
  empty chassis frame using `design-system.css` — sufficient to prove
  the WebView pipe.
- `ui/gallery.html` + `ui/src/gallery.js` — the dev-only widget showroom.
  Each widget appears with a label, bound to a scratch apvts param,
  with a small "controls" strip showing the parameter's normalised
  value live.
- `ui/src/widgets/` — one module per widget. Class-based; each module
  exports a `mount(host, opts)` function that returns a controller
  with `dispose()`, `setValue(v)`, `setActive(bool)` (where applicable).
- `ui/src/binding.js` — the relay-binding helper layer
  (`bindSlider` / `bindToggle` / `bindCombo` over `window.__JUCE__`).
- `ui/src/main.js` — the v2 entry that mounts the chassis skeleton.
  Per-mode panels are stubbed empty for this task; Tasks 05–07 fill
  them in.
- `ui/src/juce/` (existing native-interop helper) carried unchanged.
- **Delete the mockup pages** at task close: `ui/mockup-chassis.html`,
  `ui/mockup-fm.html`, `ui/mockup-sq.html`, `ui/mockup-d.html` and the
  multi-page entries for them in `vite.config.js`. The gallery page
  replaces them as the visual-verification surface.

## Out of scope

- Per-mode panel assembly — Tasks 05 (FM), 06 (SQ), 07 (D).
- Header layout / status-bar layout / modals — Task 08+.
- Drag-and-drop handler / native file choosers — Task 09.
- Settings modal — Task 08.

## Implementation steps

1. Restore the `GenVstWebBundle` custom target + `juce_add_binary_data
   (GenVstWebData …)` in `src/CMakeLists.txt` (mirror v1's Task 03 wiring).
2. Rewrite `PluginEditor`:
   - Constructor: try `juce::WebBrowserComponent` initialisation with
     the v2 options (`withNativeIntegrationEnabled`, resource provider
     pointing at the binary-data zip in release / `localhost:5173` in
     dev, the relay/native-function registration scaffold for Tasks
     05–10).
   - On WebView failure (init returns false / WebView2 missing on
     Windows), construct the fallback panel native component instead.
   - Size 1200×560 (fixed).
   - `EDITOR_WANTS_KEYBOARD_FOCUS = TRUE` (already in `src/CMakeLists.txt`).
   - Wire the `uiReady` event listener so the C++ side knows the page
     has mounted.
   - Wire the `meterData` event push — the existing telemetry timer
     (~30 Hz) sends `{ peakL, peakR, noteOn }`; this task just
     re-establishes the editor-side wiring, no telemetry changes.
3. Implement each widget under `ui/src/widgets/`:
   - **`knob.js`** — CSS body + Canvas indicator arc per
     `09-visual-spec.md`'s recipe. Mounts a `<div class="knob">` with
     an inner `<canvas>`. Drag handler: vertical drag, `Shift` fine,
     double-click reset, scroll-wheel. Emits `bindSlider`'s
     `setNormalised` on each change; subscribes to
     `valueChangedEvent` for inbound updates.
   - **`decimator-knob.js`** — `knob` with `class="knob decimator-knob"`
     and a larger size (96 px). The CSS recipe in
     `09-visual-spec.md` swaps the top sheen for a flat
     `--knob-body-dark`. Same interaction handler.
   - **`button.js`** — pure CSS button per the recipe. Press feedback
     `:active`. Emits a `click` event; if bound to a toggle param via
     `bindToggle`, flips and writes the param.
   - **`stepper.js`** — CSS frame with an inner canvas LCD-readout for
     the value, plus two `<button>` arrows. Click / hold / scroll
     increments via `bindSlider`.
   - **`lcd-readout.js`** — canvas-only widget; takes `value` (number
     or string), draws background + two-pass glowing text per the
     `09-visual-spec.md` recipe.
   - **`patch-name-lcd.js`** — `lcd-readout` at a larger size,
     designed for the header. Exposes `setText(...)`.
   - **`toggle-switch.js`** — CSS-only on/off toggle with the lit
     outer glow. Two-position labelled variant (`legacy / crystal
     clear`) accepted via opts.
   - **`slider.js`** — CSS-only horizontal slider; drag handler same
     as knob but horizontal.
   - **`algo-grid.js`** — DOM/CSS-only: an 8-button picker (2 × 4 grid)
     bound to apvts param `algorithm` (0..7). The selected button
     carries `.is-active`. Recipe in `09-visual-spec.md` *Algorithm
     picker — `algo-grid`*. No popover; all 8 buttons visible all the
     time.
   - **`algorithm-mini.js`** — Canvas, **read-only**: 8 hard-coded
     operator-routing diagrams (~112 px tile size). Renders the
     currently-selected algorithm's topology. No click handler — the
     picker is `algo-grid.js` above. `setAlgorithm(idx)` updates the
     drawing.
   - **`envelope-curve.js`** — Canvas: takes 5 ADSR-like envelope param
     values + computes the curve + draws the segment labels (`AR`,
     `DR`, `SL`, `SR`, `RR`) at each segment midpoint and the dashed
     vertical `KEY ON` / `KEY OFF` markers per `09-visual-spec.md`
     *Envelope curve (canvas)*. `setEnvelope(ar, dr, sl, sr, rr)`
     recomputes and redraws.
   - **`note-on-led.js`** — DOM/CSS-only `.note-on` cluster: a 16 px
     round `.note-on-led` paired with a stacked `NOTE ON` text caption.
     The LED lights when the bound telemetry boolean is true; the text
     caption is static. Recipe in `09-visual-spec.md` *NOTE ON LED*.
   - **`level-meter.js`** — Canvas: row of LED segments per the
     recipe. `setLevel(0..1)` updates; subscribes to the `meterData`
     event for live data, or accepts an explicit data feed for the
     gallery.
   - **`op-badge.js`** — CSS-only badge. `setActive(bool)` toggles the
     outer-glow state.
   - **`notification-toast.js`** — listens for the `notify` event
     `{ level, message }`; renders a toast that auto-dismisses after
     4 s; stacks up to 2 at a time, queues more.
4. Implement the binding layer:
   - `bindSlider(name, opts)` — returns a `{ getNormalised(),
     setNormalised(v), beginGesture(), endGesture(), onChange(cb),
     defaultNormalised(fallback) }` controller.
   - `bindToggle(name, opts)` — `{ get(), set(b), onChange(cb) }`.
   - `bindCombo(name, opts)` — `{ getIndex(), setIndex(i), onChange(cb),
     choices }`.
   - The implementations wrap JUCE 8's relay/state objects from
     `window.__JUCE__.getSliderState(name)` etc.
5. Implement `gallery.html` + `gallery.js`:
   - One section per widget; each mounts the widget bound to a scratch
     apvts param (`gallery_knob_a`, `gallery_toggle_a`, etc.).
   - Display the parameter's current normalised value in a sibling
     `<div>` so the two-way binding is visible.
6. Add scratch apvts params (`gallery_knob_a..d`, `gallery_toggle_a..d`,
   `gallery_combo_a` …) to `createParameterLayout`. Group them in a
   `"GALLERY"` sub-tree so they're obvious as developer params; the
   host still shows them in its parameter list, but the values are
   irrelevant.
7. Implement `index.html` (production main entry): full chassis frame
   from `design-system.css`; per-mode panels stub-empty (`<main
   id="mode-panel"></main>`). Mounts the notification-toast host. Fires
   the `uiReady` event.
8. Update `vite.config.js` multi-page entries: `main` →
   `index.html`, `gallery` → `gallery.html`. **Drop** all
   `mockup-*.html` entries.
9. Delete the four mockup HTML files (`mockup-chassis.html`,
   `mockup-fm.html`, `mockup-sq.html`, `mockup-d.html`) and any
   throwaway mockup CSS files.

## Deliverables

- `ui/src/widgets/{knob,button,stepper,lcd-readout,patch-name-lcd,
  toggle-switch,slider,algorithm-mini,envelope-curve,note-on-led,
  level-meter,decimator-knob,op-badge,notification-toast}.js`.
- `ui/src/binding.js`, `ui/src/main.js`, `ui/src/gallery.js`.
- `ui/index.html`, `ui/gallery.html` (re-introduced).
- Updated `ui/vite.config.js` (multi-page = main + gallery only;
  mockup pages removed).
- Updated `src/CMakeLists.txt` (`GenVstWebBundle`, `GenVstWebData`).
- Updated `src/PluginEditor.{h,cpp}` (WebView host + fallback panel).
- Updated `src/PluginProcessor.cpp` `createParameterLayout` — scratch
  gallery params added.
- Mockup HTML files deleted.

## Verification

1. `cmake --build build/windows-debug` builds clean, including the
   Vite UI bundle (CMake invokes `npm ci && npm run build` per
   `06-build-system.md`).
2. Release build: open the Standalone — WebView shows the empty v2
   chassis (`index.html`); no console errors in the WebView devtools.
3. Dev build (`-DGENVST_DEV_SERVER=ON`, `npm run dev` in `ui/`):
   - Open the Standalone — WebView loads `localhost:5173/index.html`.
   - Open `localhost:5173/gallery.html` in a separate Chromium tab —
     every widget renders, every widget moves when dragged / clicked /
     scrolled, the displayed parameter value updates two-way (changing
     the value in the DAW's generic editor moves the widget; moving
     the widget updates the DAW lane).
4. Widget acceptance checklist:
   - [ ] Knob: vertical drag, `Shift` fine, double-click reset, scroll
         wheel; indicator sweep ~270°, rest at 7 o'clock.
   - [ ] Decimator-knob: same as knob, larger, matte-black body (no
         top sheen).
   - [ ] Button press feedback: scale 0.97 + inset shadow on `:active`.
   - [ ] Stepper: ▲/▼ click increments; click-and-hold repeats after
         ~300 ms; scroll wheel = ±1.
   - [ ] LCD-readout / patch-name-lcd: glowing text on deep navy bg;
         text changes when the bound value changes.
   - [ ] Toggle-switch lit state has the outer glow.
   - [ ] Algo-grid: 8 visible buttons (2 × 4); clicking a button sets
         `algorithm` and turns that button `is-active`; the
         algorithm-mini tile next to it redraws to show the selected
         topology.
   - [ ] Algorithm-mini: changes the drawn topology in response to
         `setAlgorithm(idx)`. No click handler, no popover.
   - [ ] Envelope-curve: changing any of the 5 envelope sliders
         redraws the curve; segment labels (`AR`/`DR`/`SL`/`SR`/`RR`)
         and dashed `KEY ON` / `KEY OFF` markers stay in the right
         positions as the envelope shape changes.
   - [ ] Note-on cluster: the 16 px LED lights when the gallery's
         scratch boolean is true (the gallery includes a manual toggle
         wired to the LED); the `NOTE ON` text caption is always
         visible beneath it.
   - [ ] Level-meter responds to a slider-driven `level` value in the
         gallery; segment colour shifts to `--led-on-warm` on the last
         2 segments at peak.
   - [ ] Op-badge `setActive(true)` lights the outer glow.
   - [ ] Notification toast: pushing a fake `notify` event from the
         gallery slides a toast in; it auto-dismisses after ~4 s;
         clicking dismisses immediately; stacking caps at 2.
5. WebView fallback: rename / hide the WebView2 runtime locally (or
   the `webkit2gtk` package on Linux); the editor falls back to the
   native panel from Task 02; clicking *Retry* tries again.
6. `pluginval --strictness-level 8 --validate "<path>/Gen VST.vst3"` —
   passes.

## Done when

- [ ] Every widget in `05-ui-ux.md` *Component Inventory* exists under
      `ui/src/widgets/` and is bound through `ui/src/binding.js`.
- [ ] The gallery page exercises every widget against scratch params.
- [ ] The WebView editor loads on both Windows and macOS (Mac smoke
      test only; full cross-platform QA is Task 10).
- [ ] The fallback panel shows when the WebView fails; Retry recovers
      it.
- [ ] All mockup HTML files are deleted; `design-system.css` carries
      forward.
- [ ] `pluginval` strictness 8 passes; `ctest` is green.
