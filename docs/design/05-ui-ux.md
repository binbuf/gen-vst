# UI/UX Design

## UI Strategy: JUCE 8 WebView

The Gen VST interface is built as an **HTML/CSS/JS application rendered in a WebView**,
hosted by `juce::WebBrowserComponent` (JUCE 8.0.4). It is **not** a native
`LookAndFeel_V4` UI. This supersedes the earlier native-UI design.

This document covers UI **strategy** and the C++↔JS contract; every individual
**view** — the main FM/SQ/D sections, the patch browser, and all modals and
dialogs — is specified in [08-ui-views.md](08-ui-views.md).

**Why WebView:**
- The target aesthetic (`docs/genny-ui.md`) is pixel-art skeuomorphic — custom knobs,
  green-LCD panels, segment displays, live-drawn envelope/algorithm graphics. This is
  far faster to build and iterate in HTML/CSS + Canvas than in C++ `juce::Graphics`.
- Hot-reload: the UI can be edited live against a running plugin (dev server), instead
  of recompiling C++ for every visual tweak.
- The whole web ecosystem (layout, fonts, canvas) is available.
- JUCE 8 provides first-class two-way binding between HTML controls and the
  `AudioProcessorValueTreeState`, plus a C++→JS event channel.

**Tradeoffs accepted:** WebView2 runtime dependency on Windows; WebKitGTK dependency on
Linux; larger plugin bundle (embedded web assets); the UI and audio code are in two
languages. These are considered worthwhile for the iteration speed and visual ceiling.

`juce::WebBrowserComponent` is a different engine on each platform — WebView2
(Chromium) on Windows, WKWebView on macOS, WebKitGTK on Linux. The support
matrix, minimum runtime versions, the macOS deployment target, and the decision
that **functional parity is required but visual pixel-parity is a non-goal** are
set by [ADR-0015](adr/0015-webview-backend-support.md). Windows runtime delivery
is [ADR-0016](adr/0016-webview2-runtime-distribution.md); display scaling is
[ADR-0017](adr/0017-hidpi-display-scaling.md).

**Frontend stack:** vanilla JavaScript + Canvas, bundled by **Vite**. No UI framework —
the interface is mostly custom canvas-drawn widgets, so a component framework adds
weight without much benefit. Vite provides the dev server (hot reload) and the
production bundle.

---

## Visual Direction

The interface mimics the Genny VST UI specified in `docs/genny-ui.md` — that document
is the authoritative visual spec. Summary of what is adopted:

- **Aesthetic:** pixel-perfect skeuomorphic late-80s/early-90s rackmount FM synth — dark
  plastic chassis, glowing green CRT/LCD insets, chunky blue knobs, red segment/LED
  readouts. Everything pixel-snapped: `image-rendering: pixelated`, no `border-radius`,
  1–2px solid borders faking bevels, no smooth gradients or drop shadows.
- **Palette, typography, knob/LCD/LED styling:** exactly as in `docs/genny-ui.md`
  (chassis `#000000`, LCD olive-green base with phosphor-green pixels, LED red `#ff2020`,
  knob blue `#4a78c8`, bitmap label font, 7-segment + 5×7 dot-matrix specialty fonts).
- **The one deviation:** the header wordmark reads **"GEN VST"**, not "GENNY". The
  plugin display name is **"Gen VST"** (with a space). All other branding glyphs
  (beveled gold logotype, red underline) follow the Genny style.

### Layout

Fixed window, **960 × 560 px**, no resize for the foreseeable future (the pixel-art
design assumes a fixed grid). Four regions, per `docs/genny-ui.md`:

```
┌───────────────────────────────────────────────────────────────┐
│  GEN VST   [VU]        [ ▌ RED 7-SEG PATCH-NAME DISPLAY ▐ ]    │ header ~80px
├──────────────┬───────────────────────────┬────────────────────┤
│ LFO / AMS /  │  INSTRUMENTS (green LCD    │  PRESETS | IMPORT  │
│ FMS knobs    │  list) + channel routing  │  (green LCD list)  │ middle row
│ ALGORITHM    │  FM/SQ/D · CH 1-6 · MIDI  │                    │
│ 1-8 + diagram│  TRANSPOSE · RNG · DEL·PAN│                    │
├──────────────┴───────────────────────────┴────────────────────┤
│  [ OP1 panel ] [ OP2 panel ] [ OP3 panel ] [ OP4 panel ]       │ bottom ~220px
└───────────────────────────────────────────────────────────────┘
```

Each operator panel (bottom row) contains, top→bottom: operator badge, a green-LCD
**ADSR envelope graph**, five knobs (`ATK DR1 SUS DR2 RR`), and four sliders
(`DETUNE FREQ ENV-SCALE LFO/SSG`) with red LED readouts — see `docs/genny-ui.md` for the
exact per-panel spec.

---

## Rendering & Asset Strategy

The UI ships with **no bitmap image assets** (no sprite sheets, no PNG panels, no
texture files). Every pixel on screen is produced one of two ways:

- **HTML + CSS** — page structure, region layout, the static chassis, panel insets
  and faked bevels. CSS handles anything that does not change at runtime.
- **Canvas 2D** — every custom or dynamic widget (knobs, sliders, LCD lists, the
  algorithm diagram, ADSR graphs, the oscilloscope, VU meter, segment/LED readouts).
  See the Component Inventory below.

**No SVG.** SVG anti-aliases by default and works against the pixel-snapped look;
canvas + CSS cover every case. **No WebGL** — Canvas 2D is sufficient at 960×560.

The only binary assets bundled with the web build are **fonts** (see *Fonts* below).

### Pixel-Art Style Rules

This is *not* a Sega Genesis game screen — `docs/genny-ui.md` describes a fictional
late-80s **rackmount FM synth chassis**. There is no literal Genesis hardware
constraint in a WebView. What we adopt instead is a **self-imposed pixel-art
discipline** inspired by 16-bit-era graphics. These rules are binding for all UI
work:

- **1× pixel grid.** Author everything at logical 1× resolution; the WebView only
  ever scales by **integer** factors (1×/2×/3×). No fractional scaling.
- **No anti-aliasing.** Set `image-rendering: pixelated` on the root and all
  `<canvas>` elements; set `ctx.imageSmoothingEnabled = false`. Draw 1px strokes on
  integer/half-pixel coordinates so they stay crisp.
- **No `border-radius`.** All corners are square.
- **Bevels are hard borders**, not shadows: 1–2px solid light edge (top/left) and
  dark edge (bottom/right) — inverted for recessed insets. No `box-shadow` blur; a
  shadow, if any, is a single 1px hard solid offset.
- **No smooth gradients.** Shading and blends are done with **ordered dithering**
  (2×2 / 4×4 Bayer checkerboards of two palette colors), never CSS gradients.
- **8px base grid.** Component sizes, padding and spacing snap to multiples of 8
  (4px only where unavoidable). This is what gives the layout its tile-aligned feel.

### Color

The palette table in `docs/genny-ui.md` (chassis, LCD, LED, knob, logo, label
colors) is the **single authoritative source**. Colors are referenced as named CSS
custom properties, not hand-typed hex per component. Rules:

- New colors are **added to the `genny-ui.md` palette table** first, never invented
  ad hoc inside a component.
- The palette is hand-authored for the rackmount look; it is **not** quantized to
  the Mega Drive 9-bit color space, and need not be. *Optionally*, when extending
  the palette, new colors may be snapped to the Mega Drive channel grid (8 levels
  per channel) as an authenticity discipline — but existing `genny-ui.md` values are
  authoritative even where they fall off that grid.

### Fonts

Three type styles, all bitmap/segment-based. Asset files live in **`extern/fonts/`**
and are pulled into the Vite web build (then embedded in the release bundle):

| Style | Source | How rendered |
|-------|--------|--------------|
| Label font (8px, all-caps) | **Press Start 2P** — `extern/fonts/press-start-2p/` (SIL OFL) | CSS `@font-face`, used at 8px / 16px only — never fractional sizes |
| 7-segment patch display | **torinak 7-segment** — `extern/fonts/7-segment/` (SIL OFL, from torinak.com) | CSS `@font-face`; the `seg-display` canvas widget renders text in this face |
| 5×7 dot-matrix LED readouts | *No font* — built-in glyph table (see *5×7 Dot-Matrix Readouts* below) | Canvas primitives |

The header **"GEN VST" wordmark** (beveled gold logotype, red underline) is **canvas-
drawn** from a small glyph definition — consistent with the no-image-assets rule.

### 5×7 Dot-Matrix Readouts

The small red value readouts beside knobs and sliders (`led-readout` component) are
**not** rendered with a font. A real dot-matrix LED display shows its *entire* dot
grid — the unlit dots stay faintly visible behind the lit characters — and that
unlit grid is a signature part of the look. A font only draws lit pixels, so we draw
these readouts directly on canvas. The character set is also tiny, which makes a
font's Unicode coverage pure overhead. This keeps all "LED" output (this widget and
`seg-display`) going through canvas with per-dot color control.

**Glyph table.** A built-in JS table maps each supported character to a 5-wide ×
7-tall boolean bitmap, stored as 7 row values of 5 bits each (`0bXXXXX` per row, MSB
= leftmost dot). Supported glyphs are exactly what the readouts can display:

```
digits  0 1 2 3 4 5 6 7 8 9
letters O F            (for "OFF")
symbols -              (negative values)
blank   ' '            (all dots unlit)
```

Any value the UI cannot map to these glyphs is a bug in the calling widget, not a
fallback case — readouts only ever show short numerics, `OFF`, or signed numbers.

**Geometry.** Two named constants, chosen so dots stay pixel-snapped at every
integer window scale (`DOT_PITCH > DOT_SIZE` gives the inter-dot gap):

| Constant | Suggested 1× value | Meaning |
|----------|--------------------|---------|
| `DOT_SIZE`  | `1` px | side length of one lit/unlit dot square |
| `DOT_PITCH` | `2` px | center-to-center spacing of adjacent dots |

One glyph therefore occupies `5·DOT_PITCH − (DOT_PITCH − DOT_SIZE)` px wide ×
`7·DOT_PITCH − (DOT_PITCH − DOT_SIZE)` px tall. Characters are separated by **one
empty dot column** (`DOT_PITCH` px). Final sizes are tuned during implementation
against `docs/genny-ui.md`; the doc fixes the *method*, not the exact pixels.

**Render algorithm.** Given a string value:

1. Right-align the string in the readout's fixed character width (LED readouts
   right-justify numbers); pad on the left with `blank` glyphs.
2. For every dot cell across the full `widthChars × 5` by `7` grid, fill a
   `DOT_SIZE` square at its pitch-derived integer coordinate:
   - **unlit** dot → `--led-dim` (dark red, e.g. the `#2a0808` segment-base color);
   - **lit** dot → `--led-on` (`#ff2020`).
3. Optional **bloom**: a lit dot may tint its 4 orthogonal neighbors with a dim red
   via the 2×2 Bayer dither from the pixel-art rules — *never* a blur. Off by
   default; enabled per-readout only if needed to match the reference.

The readout repaints whenever its bound value changes — same JS `valueChangedEvent`
path as every other widget; no animation, just an instant pixel redraw.

`--led-dim` and `--led-on` are added to the `genny-ui.md` palette table per the
*Color* rules above.

---

## Component Inventory (frontend)

All custom widgets are Canvas-drawn for pixel-accurate control. Components:

| Component | Role |
|-----------|------|
| `knob` | Blue skeuomorphic rotary; ~270° sweep, rest at 7 o'clock; vertical click-drag (up = increase), shift = fine, double-click = reset |
| `slider` | Horizontal groove + chunky blue cap, red LED readout |
| `seg-display` | Red 7-segment patch-name display in the header |
| `led-readout` | Small red 5×7 dot-matrix value readout beside knobs/sliders; canvas-drawn from a glyph table, renders lit + unlit dots — see *Rendering & Asset Strategy → 5×7 Dot-Matrix Readouts* |
| `vu-meter` | Small green "TRUE STEREO" VU in the header |
| `lcd-list` | Green-LCD scrollable list (Instruments, Presets) — pixel scrollbar, inverse-video selection |
| `algo-diagram` | 8 hard-coded YM2612 routings drawn into a green-LCD inset; redraws on ALG change; colours carriers vs modulators |
| `adsr-graph` | Per-operator envelope curve, computed **analytically in JS** from the five envelope values — no C++ round-trip; redraws on value change |
| `oscilloscope` | Waveform of recent mixed output (fed by C++→JS push) |
| `algo-buttons` | 8 numbered buttons; selected one wrapped in a red stamped ring |
| `operator-panel` | Composite: badge + adsr-graph + 5 knobs + 4 sliders |
| `step-field` | Numeric value with up/down arrows (MIDI ch, transpose) |
| `section-tabs` / `pill-buttons` | FM/SQ/D selector, PRESETS/IMPORT tabs |
| `notification-toast` | Transient error/warning banner — bad patch file, rejected DMP version, missing custom root; shown on a C++→JS `notify` event, auto-dismisses |

Live redraws of knobs, the algorithm diagram, and ADSR graphs are driven entirely by
JS-side parameter-change events (see below) — only the oscilloscope and VU need data
pushed from C++.

---

## C++ Integration Contract

The UI is hosted and bound by the plugin editor. This section defines the contract the
UI depends on; the editor implementation itself is a later doc/iteration.

### Editor host

`GenVstAudioProcessorEditor` owns a single `juce::WebBrowserComponent` sized to the
whole 960×560 window. The browser is configured (`WebBrowserComponent::Options`) with:
- `withNativeIntegrationEnabled()` — injects the `juce` JS module (`window.__JUCE__`).
- `withResourceProvider(...)` — serves the embedded web bundle in release builds.
- Parameter **relays** registered via `withOptionsFrom(...)`.
- **Native functions** for non-parameter actions (see below).
- An event listener for a `uiReady` event the page fires once mounted.

**File drag-and-drop.** Importing patches by dropping `.tfi`/`.vgi`/`.dmp` files —
or a folder — onto the plugin window needs the real filesystem path of each
dropped item. An HTML5 drop inside the WebView only yields `File` objects, not
paths (and cannot enumerate a dropped folder), so drag-and-drop is handled by a
native `juce::FileDragAndDropTarget` on the editor, **not** by HTML5 drop events.
The editor forwards the resolved paths to the patch system.

`EDITOR_WANTS_KEYBOARD_FOCUS` must be `TRUE` (the HTML UI has text/numeric inputs) —
note this differs from the value in `docs/design/06-build-system.md`.

### Parameter binding (two-way)

Every automatable parameter lives in the `apvts` (see `docs/design/01-architecture.md`
and `07-feature-spec.md`). Each UI control is bound to its parameter through a JUCE
**relay + attachment** pair:

| UI control kind | Relay | Attachment |
|-----------------|-------|------------|
| knob / slider | `WebSliderRelay` | `WebSliderParameterAttachment` |
| toggle / LED button | `WebToggleButtonRelay` | `WebToggleButtonParameterAttachment` |
| selector / combo | `WebComboBoxRelay` | `WebComboBoxParameterAttachment` |

The binding is fully two-way: a knob drag in HTML moves the `apvts` parameter (and the
DAW automation lane); a host-side automation change repaints the knob. On the JS side,
controls subscribe to `valueChangedEvent` on their relay state — this is what drives
live knob/diagram/ADSR-graph redraws with no extra plumbing.

**Naming contract:** the relay name equals the `apvts` parameter ID (part-stripped
for FM-part params — see paging). A single binding table is the source of truth for
both C++ and the JS UI.

### FM channel paging

The Genny layout edits **one FM part at a time** (the 1–6 channel selector), but
the `apvts` holds parameters for all 6 parts ([ADR-0013](adr/0013-multitimbral-voice-model.md)).
FM-part-scoped relays are named **without** the `_part<n>` suffix (e.g. `atk_op1`).
When the user selects a different part:

1. JS calls the `selectChannel(n)` native function.
2. The editor rebuilds the FM attachments so each FM relay re-binds to part `n`'s
   `apvts` parameter.
3. Rebinding pushes the new parameter values into the relays → JS repaints every knob,
   slider, LED and the algorithm diagram in one batch — matching the "selecting an item
   repaints everything" behaviour in `docs/genny-ui.md`.

Global, PSG and DAC relays bind once and never rebind. The `FM / SQ / D` section
selector swaps which control set the bottom region shows via a `selectSection` native
function.

### Native functions (non-parameter actions)

Actions that are not `apvts` parameters are exposed as JUCE native functions, callable
from JS and returning a Promise:

| Function | Purpose |
|----------|---------|
| `selectChannel(n)` | Switch the edited FM channel (triggers attachment rebind) |
| `selectSection(s)` | FM / SQ / D section switch |
| `getPatchList()` | Populate the Instruments / Presets LCD lists |
| `loadPreset(id)` / `loadInstrument(id)` | Apply a stored patch to the current channel |
| `savePatch()` / `importPatch()` / `exportPatch()` | Patch persistence + file choosers |
| `midiPanic()` | All-sound-off |

### C++ → JS telemetry push

The oscilloscope, VU meter, clip LED and per-voice activity LEDs need data that has no
`apvts` parameter. The processor owns lock-free telemetry written by the audio thread:
- Oscilloscope: a single-producer/single-consumer ring buffer (`juce::AbstractFifo`).
- VU levels, clip flag, voice key-on mask: `std::atomic` scalars.

A `juce::Timer` in the editor (~30 Hz) reads this telemetry on the message thread,
downsamples the scope buffer (~768 points), and pushes one combined event —
`emitEventIfBrowserIsVisible("meterData", { scope, vuL, vuR, clip, voiceMask })`. The JS
side subscribes with `addEventListener("meterData", …)`. Telemetry is processor-owned so
the editor can be opened/closed independently of audio.

### C++ → JS notifications

Patch-load failures and similar conditions — an unreadable file, a DMP file
rejected for its version ([ADR-0012](adr/0012-dmp-version-scope.md)), a custom
patch root that no longer resolves — are surfaced to the user. The processor (or
editor) pushes a `notify` event —
`emitEventIfBrowserIsVisible("notify", { level, message })` — and the JS side
renders it as a transient `notification-toast`. This is the single user-visible
error channel the rest of the design refers to.

### Resource delivery & dev workflow

- **Release:** the Vite production bundle is zipped and embedded in the plugin binary;
  the editor's resource provider serves files out of the zip.
- **Development:** the editor loads `http://localhost:5173` (Vite dev server) instead,
  selected by a compile flag. JUCE still injects the `juce` module for the localhost
  page, so relays and native functions work live — HTML/CSS/JS can be edited with
  instant hot reload against the running plugin (fastest in the Standalone build).
- **Resource MIME types:** the resource provider must return a correct
  `Content-Type` for every served asset, including `.woff2`/`.ttf` fonts. The
  WebKit backends (macOS, Linux) reject `@font-face` files served with a wrong or
  missing MIME type where Chromium is lenient — an incorrect type would surface as
  missing fonts on macOS/Linux only ([ADR-0015](adr/0015-webview-backend-support.md)).

---

## Comparison to Genny VST

| Aspect | Genny | Gen VST |
|--------|-------|---------|
| UI framework | VSTGUI, native | JUCE 8 WebView (HTML/CSS/JS) |
| Aesthetic | Pixel-art skeuomorphic rack synth | Mimics Genny (`docs/genny-ui.md`); wordmark "GEN VST" |
| Algorithm diagram | Static | Live-drawn, recolours carriers/modulators on ALG change |
| Envelope display | Per-operator LCD graph | Per-operator LCD graph, analytic redraw in JS |
| Oscilloscope | — | Yes (C++→JS telemetry push) |
| Parameter automation | — | Every control is an `apvts` param → full DAW automation |
| Patch browser | Instruments + Presets lists | Same, backed by native functions |

---

## Open Questions

1. **Window scaling** — *Resolved.* Integer scale presets (1×/2×/3×) with
   nearest-integer snapping on fractional-DPI displays — see
   [ADR-0017](adr/0017-hidpi-display-scaling.md).
2. **WebView2 runtime fallback** — *Resolved.* A Windows installer bundles the
   WebView2 Evergreen Bootstrapper; if the WebView still fails to initialise the
   editor shows a native fallback panel — see
   [ADR-0016](adr/0016-webview2-runtime-distribution.md) and
   [08-ui-views.md](08-ui-views.md) (view 9).
3. **Specialty fonts** — *Resolved.* See *Rendering & Asset Strategy → Fonts*. Label
   font = Press Start 2P; 7-segment = torinak 7-segment font; 5×7 dot-matrix readouts
   are canvas-drawn (no font). All in `extern/fonts/`; both fonts are SIL OFL.
4. **`06-build-system.md` follow-up** — *Resolved.* `06-build-system.md` has been
   rewritten for the WebView build: `JUCE_WEB_BROWSER=1`, `NEEDS_WEBVIEW2`,
   `juce_gui_extra`, the Vite/CMake web-bundle pipeline, and the WebKitGTK Linux
   dependency.
5. **Patch list ↔ part** — *Resolved.* Loading an Instrument/Preset targets the
   **currently selected part** ([ADR-0013](adr/0013-multitimbral-voice-model.md)). A
   multi-part "performance" file that loads all 6 parts at once is out of MVP scope.
6. **Section parity** — *Resolved.* The SQ (PSG) and D (DAC) sections are fully
   specified in [08-ui-views.md](08-ui-views.md) (views 2 and 3); they are not
   stubbed in the design.
