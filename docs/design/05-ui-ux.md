# UI/UX Strategy (v2)

> **Note.** The v1 pixel-art skeuomorphic UI is archived at
> `docs/design/archive/v1-05-ui-ux.md`. v2 replaces it with the
> modern hardware-VST direction below. The view-level layout per mode lives
> in [`08-ui-views.md`](08-ui-views.md); the authoritative visual reference
> (palette, fonts, exact CSS recipes per widget) lives in
> [`09-visual-spec.md`](09-visual-spec.md).

## UI Strategy: JUCE 8 WebView (unchanged from v1)

The Gen VST interface is built as an **HTML/CSS/JS application rendered in
a WebView**, hosted by `juce::WebBrowserComponent` (JUCE 8.0.4). It is
**not** a native `LookAndFeel_V4` UI. This was [ADR-0001](adr/0001-juce8-webview-ui.md);
the WebView host and the JUCE binding contract carry into v2 untouched.

**Why WebView (carried from v1):**
- Modern VST chassis aesthetics (gradients, layered shadows, smooth knob
  rendering) are far faster to author in HTML/CSS + Canvas than in C++
  `juce::Graphics`.
- Hot-reload: the UI can be edited live against a running plugin (dev
  server), instead of recompiling C++ for every visual tweak.
- The whole web ecosystem (layout, fonts, canvas) is available.
- JUCE 8 provides first-class two-way binding between HTML controls and
  the `AudioProcessorValueTreeState`, plus a C++→JS event channel.

**Tradeoffs accepted:** WebView2 runtime dependency on Windows; WebKitGTK
on Linux; larger plugin bundle (embedded web assets); the UI and audio
code are in two languages. These are still considered worthwhile.

`juce::WebBrowserComponent` is a different engine on each platform —
WebView2 (Chromium) on Windows, WKWebView on macOS, WebKitGTK on Linux.
The support matrix, minimum runtime versions, the macOS deployment target,
and the decision that **functional parity is required but visual
pixel-parity is a non-goal** are set by
[ADR-0015](adr/0015-webview-backend-support.md). Windows runtime delivery
is [ADR-0016](adr/0016-webview2-runtime-distribution.md); display scaling
is [ADR-0017](adr/0017-hidpi-display-scaling.md).

**Frontend stack:** vanilla JavaScript + Canvas, bundled by **Vite**. No
UI framework — most widgets are custom canvas-drawn or thin DOM
components. Vite provides the dev server (hot reload) and the production
bundle.

---

## Visual Direction (v2)

The v2 visual direction is a **modern hardware-VST aesthetic**, modelled
on Inphonik's **RYM2612** (FM mode reference) and **PCM2612 Retro
Decimator Unit** (D mode reference). See `09-visual-spec.md` for the
authoritative palette / fonts / per-widget recipes; this section sets the
**principles** that every widget must obey.

### Binding principles ([ADR-0022](adr/0022-modern-vst-aesthetic.md))

1. **Consistent light source — top-left.** Every shadow and bevel honors
   this single light direction. No "let the artist choose per widget."
2. **Layered shadows for depth.** A chassis or recessed inset uses 2–4
   stacked `box-shadow`s: a tight dark inner shadow + a wider soft outer
   shadow + optionally a 1px light highlight on the lit edge. No single
   hard 1px bevel; no blurry single-shadow "drop shadow" effect on its
   own.
3. **Gradients on physical surfaces.** Knob bodies, button caps, the
   chassis itself, and slider thumbs use `linear-gradient` to mimic
   plastic or brushed metal. Flat fills are reserved for LCDs (where a
   flat dark base reads as "screen") and pure-text labels.
4. **Press feedback = scale + inset shadow.** Buttons depress on
   `:active` via a `transform: scale(0.97)` plus an `inset` `box-shadow`,
   paired with a short `transition` (≤120 ms) so the motion feels
   weighted rather than instantaneous.
5. **Typography: monospace labels with letter-spacing.** Labels (`AR`,
   `DR`, `MUL`, `FREQ`, `ALGORITHM`, etc.) render in a monospaced font
   with wide `letter-spacing` (≈ 0.1–0.2 em). LCD displays use a
   dedicated LCD-style typeface; the patch name in the header is the
   most prominent example.
6. **Antialiasing is on.** Browser default. Canvas widgets enable
   `imageSmoothingEnabled = true` and draw with antialiased strokes.
7. **`border-radius` allowed**, but used sparingly — a 2–4 px radius on
   buttons and pill toggles, a slightly larger radius on chassis panels.
   Pure-square corners remain idiomatic for LCD insets.
8. **No ordered dithering.** That was a pixel-art technique; gradients
   carry smoothly.

These rules supersede the v1 "1× pixel grid / no `border-radius` / hard
1px bevels / ordered dithering" discipline wholesale.

### Layout

Fixed window, **1200 × 560 px** ([ADR-0023](adr/0023-fixed-window-1200x560.md)).
Two top-level regions:

```
┌───────────────────────────────────────────────────────────────────────────────────┐
│  Header (~88 px)                                                                   │
│  [● NOTE ON][logo] [mode FM SQ D] [patch-LCD ◀ ▶ 📂] [Filter] [Ladder]            │
│                                                          [L ▮▮▮▮ / R ▮▮▮▮] [VOL] [⚙]│
├───────────────────────────────────────────────────────────────────────────────────┤
│  Mode panel — main editing area (~470 px)                                          │
│  Contents depend on active mode (FM / SQ / D); see 08-ui-views.md                  │
└───────────────────────────────────────────────────────────────────────────────────┘
```

`●` is the NOTE ON LED + visible text label (moved to the header in v2 to
mirror the RYM2612 reference, sized to be readable); `L ▮▮▮▮ / R ▮▮▮▮` is
the stacked output level-meter cell (replaces the v1-style bottom status
bar — removed during the post-mockup review); `VOL` is the master-output
knob bound to `master_volume`.

The mode panel is the only region that swaps when the user changes mode;
the header persists. The v2-first-pass status bar that briefly carried
the L/R meters and a `v0.2.0` version string is gone — version moved to
the About modal (`08-ui-views.md` view 7), L/R meters moved into the
header cluster above. Full per-mode layouts are in
[`08-ui-views.md`](08-ui-views.md).

---

## Asset Strategy

The UI ships with **no bitmap image assets** (no sprite sheets, no PNG
panels, no texture files). Every pixel on screen is produced one of two
ways:

- **HTML + CSS** — page structure, region layout, the static chassis,
  panel insets and bevels, knob bodies, button caps, LCD chassis. CSS
  handles anything that does not change at runtime.
- **Canvas 2D** — dynamic widgets only: knob indicator arc, ADSR curve,
  algorithm diagram, level meters, the LCD readout text. See *Component
  Inventory* below for which widgets are CSS vs Canvas.

**No SVG.** Two reasons: (1) the CSS recipes in `09-visual-spec.md` use
multi-stop gradients + layered shadows that read better than an SVG
gradient, (2) keeping zero binary image assets makes the bundle leaner
and easier to hot-reload. **No WebGL** — Canvas 2D is plenty at 1200×560.

The only binary assets bundled with the web build are **fonts** (see
*Fonts* below).

### Fonts

Two type styles. Asset files live in **`extern/fonts/`** and are pulled
into the Vite web build (then embedded in the release bundle):

| Style | Source | How rendered |
|-------|--------|--------------|
| Label / control text | **IBM Plex Mono** or **JetBrains Mono** (SIL OFL) — exact choice in `09-visual-spec.md` | CSS `@font-face`, used at multiple sizes with wide `letter-spacing` |
| LCD displays (patch name, numeric readouts) | A monospaced LCD-style face — exact choice in `09-visual-spec.md` | CSS `@font-face`, used in `<canvas>` text-rendering for the LCD glow effect |

The v1 fonts (Press Start 2P, torinak 7-segment) are retired from the
active build but remain in `extern/fonts/` as unloaded assets.

The v1 5×7 dot-matrix Canvas glyph renderer is **removed** — v2 LCD
readouts use the LCD-style typeface drawn into Canvas with a subtle blur
+ glow filter for the phosphor effect.

---

## Component Inventory (frontend)

| Component | Role | DOM / Canvas |
|-----------|------|---|
| `knob` | Skeuomorphic rotary; gradient body, white indicator line; ~270° sweep, rest at 7 o'clock; vertical click-drag (up = increase), shift = fine, double-click = reset | CSS body + Canvas indicator arc |
| `button` | Pill / square buttons with depressed-on-click feedback; LED-illuminated when active | CSS only |
| `stepper` | Compact LCD numeric readout flanked by ▲/▼ buttons; click-and-hold to repeat; scroll-wheel increment. JS module is `ui/src/widgets/stepper.js`; the CSS recipe is `.stepper-readout` (see `09-visual-spec.md`). The `POLY` and `RANGE` fields on the FM panel use the `is-mini` size modifier; the `RETRIG RATE` field uses the default size. | CSS + Canvas LCD readout |
| `lcd-readout` | Patch-name LCD (header) and per-knob numeric readouts; flat dark base + glowing text | Canvas |
| `toggle-switch` | Two- and three-position toggle (e.g., `CRYSTAL CLEAR / LEGACY`); physical-slider feel | CSS + a Canvas highlight |
| `slider` | Horizontal slider with chunky cap, soft groove shadow | CSS only |
| `algorithm-mini` | The 8 YM2612 algorithm topologies, drawn small; selected one highlighted | Canvas |
| `envelope-curve` | Per-operator (FM) or per-channel (SQ) ADSR shape; computed live from envelope params | Canvas |
| `note-on-led` | Single header LED that lights on key-on (FM/SQ) or input-audio-present (D) | CSS animation on apvts-bound state |
| `midi-wheel` | Vertical wheel/slider visualizing live MIDI pitch-bend (variant `midi-wheel-pb`, center-detent) or mod-wheel (variant `midi-wheel-mw`, full-range). Read-only — driven by the host's MIDI stream, not user-draggable. Used by the FM panel's `GLOBAL IN` block (PB + MW) and the SQ panel's `GLOBAL IN` block (PB only — see `08-ui-views.md` view 3 for why SQ omits MW) | CSS + thin Canvas overlay for the centerline glow |
| `level-meter` | Stereo LED-style level bars; only deployed as the stacked `level-meter-mini` cell in the header for post-master output (no D-panel input variant in v2 — see `08-ui-views.md` view 4) | Canvas |
| `decimator-knob` | Large 96 px central knob body variant (PCM2612-style — matte body, no top sheen); identical mechanics to `knob`. Used by the D panel's central `DRY/WET` knob | CSS body + Canvas indicator arc |
| `patch-name-lcd` | Larger LCD readout in the header showing the active patch | Canvas; uses LCD-style font |
| `op-badge` | Blue-filled square showing the operator number `1..4`; click to make that operator the active target of the `envelope-curve` widget; carries an outer glow when active | CSS only |
| `notification-toast` | Transient banner for errors / warnings; same role as v1 | CSS only |
| `tooltip` | Hover descriptor for every interactive control; reads `data-tip-name` + `data-tip-desc` from the host; gated by the global `tooltips_enabled` toggle (header `TIPS` + Settings `TOOLTIPS`). Single shared DOM node reused across hovers. | CSS only |

Live redraws (knob indicator, algorithm diagram, ADSR curve, level meter)
are driven by JS-side parameter-change events — only the level meter
needs telemetry data pushed from C++.

The v1 widgets `algo-buttons`, `step-field`, `section-tabs`,
`lcd-list`, `instrument-rack`, `range-slider`, `routing-controls`,
`voice-leds`, `seg-display`, `vu-meter`, `waveform-display`,
`oscilloscope`, `clip-led`, `pixel`, `folder-icon`, `gear-icon`,
`wordmark`, `true-stereo-toggle`, `operator-panel` are all **retired**.
Some concepts return under new names (e.g., level-meter replaces
vu-meter); others are gone entirely (e.g., voice-leds — v2 has a
single note-on indicator). The v1 `tooltip` widget is **re-introduced
under the same name** but with a new content schema (see *Tooltip
system* below).

---

## Tooltip system

The global `tooltips_enabled` apvts param (header `TIPS` toggle +
Settings `TOOLTIPS` row, both bound to the same boolean, default on)
gates a single shared `.tooltip` widget that surfaces a short
descriptor when the user hovers an interactive control. The widget
recipe lives in [`09-visual-spec.md`](09-visual-spec.md) § *Tooltip*;
the **content** lives on each control as two `data-*` attributes:

| Attribute | Role | Length |
|---|---|---|
| `data-tip-name` | Full uppercase name of the control (e.g., `TOTAL LEVEL (TL)`, `DAC PRESCALER`, `FREQ CTRL MODE`). | ≤ 32 chars |
| `data-tip-desc` | One sentence describing what the control does, in plain English. | ≤ 120 chars |

The binding layer (Task 04) attaches a single hover handler on the
mode-panel root that reads the data attributes off the hovered
descendant, populates one shared `.tooltip` DOM node, and positions it
near the cursor with a ~400 ms enter delay (no delay on leave). When
`tooltips_enabled` flips off, the handler early-returns without
showing anything.

**Canonical tooltips** live in `ui/src/widgets/tooltip-content.js` as a
plain JS object keyed by widget id / param name; each widget mount
reads its entry once and writes the two attributes. The Settings
`HARDWARE STRICT` row, for example, would carry
`data-tip-name="HARDWARE STRICT (FM)"` and
`data-tip-desc="Clamp polyphony to 6 + restrict FLOAT_MUL / AUTO_RETRIG to one voice + force filter and ladder on. Matches real YM2612 constraints."`.

The data lives in code (not in `08-ui-views.md`) because the per-widget
copy is small, evolves with the widgets, and is referenced by Task 04's
widget library. The doc is the source of truth for *which* widgets get
tooltips; the JS object is the source of truth for *what they say*.

---

## C++ Integration Contract (v2)

### Editor host

`GenVstAudioProcessorEditor` owns a single `juce::WebBrowserComponent`
sized to the whole 1200×560 window. The browser is configured
(`WebBrowserComponent::Options`) with:
- `withNativeIntegrationEnabled()` — injects the `juce` JS module
  (`window.__JUCE__`).
- `withResourceProvider(...)` — serves the embedded web bundle in
  release builds.
- Parameter **relays** registered via `withOptionsFrom(...)`.
- **Native functions** for non-parameter actions (see below).
- An event listener for a `uiReady` event the page fires once mounted.

**File drag-and-drop.** Importing patches by dropping
`.tfi`/`.vgi`/`.dmp`/`.y12`/`.opm`/`.psg` files — or a folder —
onto the plugin window needs the real filesystem path of each dropped
item. An HTML5 drop inside the WebView only yields `File` objects, not
paths (and cannot enumerate a dropped folder), so drag-and-drop is
handled by a native `juce::FileDragAndDropTarget` on the editor, **not**
by HTML5 drop events. The editor forwards the resolved paths to the
patch system, which dispatches by extension via
`PatchSystem::tagFromExtension`.

`EDITOR_WANTS_KEYBOARD_FOCUS` must be `TRUE` (the HTML UI has text /
numeric inputs).

### Parameter binding (two-way) — unchanged

Every automatable parameter lives in the `apvts` (see
[`01-architecture.md`](01-architecture.md) and
[`07-feature-spec.md`](07-feature-spec.md)). Each UI control is bound to
its parameter through a JUCE **relay + attachment** pair:

| UI control kind | Relay | Attachment |
|-----------------|-------|------------|
| knob / slider | `WebSliderRelay` | `WebSliderParameterAttachment` |
| toggle / LED button | `WebToggleButtonRelay` | `WebToggleButtonParameterAttachment` |
| selector / combo | `WebComboBoxRelay` | `WebComboBoxParameterAttachment` |

The binding is fully two-way: a knob drag in HTML moves the `apvts`
parameter (and the DAW automation lane); a host-side automation change
repaints the knob. On the JS side, controls subscribe to
`valueChangedEvent` on their relay state.

**Naming contract:** the relay name equals the `apvts` parameter ID.
Under v2 these IDs are simple (no `_part<n>` suffix to strip) — the
single-engine model means one ID per parameter, full stop.

### Mode dispatch on the UI side

When the `mode_select` apvts param changes (either from a manual header
toggle or an auto-switch on patch load), the JS frame:

1. Removes any active mode panel from the DOM.
2. Mounts the new mode's panel.
3. Triggers a one-time `valueChangedEvent` replay on every relay so the
   newly mounted controls hydrate to the current apvts values.

The header (mode selector, patch-name LCD, master controls) and status
bar persist across mode switches.

The v1 *FM channel paging* mechanism — the per-part attachment-rebind
when the user selected a different FM channel — is **removed**. v2 has
no parts, so no per-part rebinding is needed.

### Native functions (non-parameter actions)

| Function | Purpose |
|----------|---------|
| `getPatchList()` | Populate the unified preset browser (returns all tagged patches across all roots) |
| `loadPatch(path)` | Apply a stored patch to the instance; auto-flips `mode_select` if the patch's tag differs |
| `savePatch()` | Save current mode's patch to the user-saved root |
| `importPatch()` / `exportPatch()` | File-chooser based import/export |
| `addPatchRoot()` | Native directory-chooser for registering a new custom root |
| `expandFolder(path)` | Lazy folder-scan for the preset browser tree; returns `{ children, perChildPatchCount }` |
| `deletePatch(path)` | Remove a patch from a writable root; refused for the Factory root |
| `patchNav(direction)` | Prev/next navigation within the active mode's sorted patch list |

MIDI panic is covered by the standard CCs (`120` All Sound Off, `123` All
Notes Off) handled in `processBlock`; no dedicated native function.

The v1 `selectChannel(n)` and `selectSection(s)` native functions are
**removed** (no parts; mode switching goes through the apvts).

### C++ → JS telemetry push

The level meter and the `NOTE ON` indicator need data that has no apvts
parameter. The processor owns lock-free telemetry written by the audio
thread:
- Level meter: peak L/R per recent window (`std::atomic<float>`).
- `NOTE ON` flag: any-voice-active boolean (`std::atomic<bool>`).

A `juce::Timer` in the editor (~30 Hz) reads this telemetry on the
message thread and pushes one combined event —
`emitEventIfBrowserIsVisible("meterData", { vuL, vuR, noteOn })`. The JS
side subscribes with `addEventListener("meterData", …)`. Telemetry is
processor-owned so the editor can be opened/closed independently of
audio.

**Live MIDI state (modwheel + pitch-bend) does not use telemetry.** The
FM panel's `GLOBAL IN` PB / MW wheels and the SQ panel's `GLOBAL IN` PB
wheel bind to the apvts parameters `mod_wheel_value` and
`pitch_bend_value` through the normal relay layer. `PluginProcessor`
mirrors every inbound CC 1 and pitch-bend message into those params via
`setValueNotifyingHost` so the widgets repaint through the same
`valueChangedEvent` path every other knob uses — no extra event, no
extra timer. The widgets are read-only (no drag handler); the apvts
params are display-only mirrors of the live MIDI stream.

The v1 oscilloscope, 16-voice LED bank, and clip LED are removed —
they belonged to the v1 chassis.

### C++ → JS notifications — unchanged

Patch-load failures and similar conditions are surfaced via a `notify`
event:
`emitEventIfBrowserIsVisible("notify", { level, message })`. The JS side
renders it as a transient `notification-toast`.

### Resource delivery & dev workflow — unchanged

- **Release:** the Vite production bundle is zipped and embedded in the
  plugin binary; the editor's resource provider serves files out of the
  zip.
- **Development:** the editor loads `http://localhost:5173` (Vite dev
  server) instead, selected by a compile flag. JUCE still injects the
  `juce` module for the localhost page, so relays and native functions
  work live — HTML/CSS/JS can be edited with instant hot reload against
  the running plugin (fastest in the Standalone build).
- **Resource MIME types:** the resource provider must return a correct
  `Content-Type` for every served asset, including `.woff2`/`.ttf`
  fonts. The WebKit backends (macOS, Linux) reject `@font-face` files
  served with a wrong or missing MIME type where Chromium is lenient.

---

## Comparison to v1

| Aspect | v1 | v2 |
|--------|----|----|
| Visual direction | Pixel-art skeuomorphic 1991 rack synth | Modern hardware-VST aesthetic (RYM2612 + PCM2612-modelled) |
| Window | 960×640 fixed | 1200×560 fixed |
| Multi-part | 6 FM parts, rack, MIDI routing matrix | Single engine per instance, three modes (FM/SQ/D) |
| Patch lists | INSTRUMENTS + PRESETS + IMPORT lists across center/right columns + a folder-tree modal | One unified tagged browser modal |
| Fonts | Press Start 2P + torinak 7-segment + custom 5×7 dot-matrix | Mono family (IBM Plex / JetBrains Mono) + LCD face |
| Aesthetic discipline | `image-rendering: pixelated`, no `border-radius`, hard 1px bevels, ordered dither | Layered shadows, gradients, antialiasing on, scale+inset press feedback |
| C++↔JS contract | WebView + relays + native fns | Same (preserved) |
| FM channel paging | Per-part rebind on channel selection | Removed (no parts) |
| Telemetry push | Oscilloscope + VU + voice mask + clip flag | Level meter + note-on flag only |

The C++↔JS binding contract is the single piece that carries forward
unchanged — every other UI choice is rebuilt against the v2 principles.
