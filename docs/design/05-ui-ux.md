# UI/UX Design

## UI Strategy: JUCE 8 WebView

The Gen VST interface is built as an **HTML/CSS/JS application rendered in a WebView**,
hosted by `juce::WebBrowserComponent` (JUCE 8.0.4). It is **not** a native
`LookAndFeel_V4` UI. This supersedes the earlier native-UI design.

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

## Component Inventory (frontend)

All custom widgets are Canvas-drawn for pixel-accurate control. Components:

| Component | Role |
|-----------|------|
| `knob` | Blue skeuomorphic rotary; ~270° sweep, rest at 7 o'clock; vertical click-drag (up = increase), shift = fine, double-click = reset |
| `slider` | Horizontal groove + chunky blue cap, red LED readout |
| `seg-display` | Red 7-segment patch-name display in the header |
| `vu-meter` | Small green "TRUE STEREO" VU in the header |
| `lcd-list` | Green-LCD scrollable list (Instruments, Presets) — pixel scrollbar, inverse-video selection |
| `algo-diagram` | 8 hard-coded YM2612 routings drawn into a green-LCD inset; redraws on ALG change; colours carriers vs modulators |
| `adsr-graph` | Per-operator envelope curve, computed **analytically in JS** from the five envelope values — no C++ round-trip; redraws on value change |
| `oscilloscope` | Waveform of recent mixed output (fed by C++→JS push) |
| `algo-buttons` | 8 numbered buttons; selected one wrapped in a red stamped ring |
| `operator-panel` | Composite: badge + adsr-graph + 5 knobs + 4 sliders |
| `step-field` | Numeric value with up/down arrows (MIDI ch, transpose) |
| `section-tabs` / `pill-buttons` | FM/SQ/D selector, PRESETS/IMPORT tabs |

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

**Naming contract:** the relay name equals the `apvts` parameter ID (channel-stripped
for FM-channel params — see paging). A single binding table is the source of truth for
both C++ and the JS UI.

### FM channel paging

The Genny layout edits **one FM channel at a time** (channel selector 1–6), but the
`apvts` holds parameters for all 6 channels. FM-channel-scoped relays are named **without**
the `_ch<n>` suffix (e.g. `atk_op1`). When the user selects a different channel:

1. JS calls the `selectChannel(n)` native function.
2. The editor rebuilds the FM attachments so each FM relay re-binds to channel `n`'s
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

### Resource delivery & dev workflow

- **Release:** the Vite production bundle is zipped and embedded in the plugin binary;
  the editor's resource provider serves files out of the zip.
- **Development:** the editor loads `http://localhost:5173` (Vite dev server) instead,
  selected by a compile flag. JUCE still injects the `juce` module for the localhost
  page, so relays and native functions work live — HTML/CSS/JS can be edited with
  instant hot reload against the running plugin (fastest in the Standalone build).

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

1. **Window scaling** — fixed 960×560 assumes 1× pixels. On HiDPI displays the WebView
   handles its own scaling; decide whether to offer integer scale presets (1×/2×) later.
2. **WebView2 runtime fallback** — behaviour on Windows machines lacking the WebView2
   runtime (show a native fallback message vs. bundle the installer).
3. **Specialty fonts** — source/license the 7-segment and 5×7 dot-matrix bitmap fonts
   (Press Start 2P covers the label font; the LED fonts are separate).
4. **`06-build-system.md` follow-up** — that doc still describes a native UI; it needs
   updating for `JUCE_WEB_BROWSER=1`, `NEEDS_WEBVIEW2`, `juce_gui_extra`, the web-bundle
   `juce_add_binary_data`, and the WebKitGTK Linux dependency.
5. **Patch list ↔ channel** — confirm whether loading an Instrument/Preset targets only
   the selected FM channel or can load a full 6-channel multi.
6. **Section parity** — whether the SQ (PSG) and D (DAC) sections get the full Genny
   treatment in the first UI build or are stubbed initially.
