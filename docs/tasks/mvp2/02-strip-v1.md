# Task 02 — Strip v1 multitimbral C++ & v1 UI

> **Milestone:** Clean baseline — v1 multitimbral C++ and v1 UI are gone;
> apvts is collapsed to single-engine; plugin still builds, loads, and
> shows a placeholder editor.
> **Depends on:** Task 01 (the mockup pages survive at `ui/mockup-*.html`
> as visual reference, even though the v1 `ui/src/` is wiped).
> **Design references:** `docs/design/01-architecture.md` (primary —
> *Retired in v2*, *Parameter System*, *Component Map*),
> `docs/design/04-patch-system.md` (single-`Patch` model), ADR-0021,
> ADR-0023, `docs/design/08-ui-views.md` view 10 (fallback panel).

## Objective

Rip out the v1 multitimbral skeleton from C++ and the v1 widget / view /
modal layer from JS so the codebase is a clean foundation for v2 UI and
DSP work. The plugin must still **build, load in a DAW, and render a
placeholder editor** at the end of this task — but it does not yet
implement any of the new v2 UI.

This is the single biggest deletion pass in the chain. After this task,
nothing in `src/` mentions parts, nothing in `ui/src/` mentions v1 widgets,
and the apvts no longer carries `_part<n>`-suffixed parameters.

## Context & key constraints

- **What's deleted in C++** (ADR-0021 *Consequences*):
  `PartManager.{h,cpp}`, `DACPlayer.{h,cpp}`, `DACKit.{h,cpp}`,
  `BankIO.{h,cpp}`, and the corresponding test files
  (`BankIOTests.cpp`, `DACKitTests.cpp`, plus any PartManager-specific
  test fixtures).
- **What's collapsed** — `MidiRouter` becomes a thin shim in
  `PluginProcessor` (or is deleted entirely if the shim is trivial):
  there is no destination table in v2; every MIDI event on the host
  channel goes to the active engine. Delete `MidiRoutingTests.cpp`.
- **What stays**: `Voice.{h,cpp}`, `VoiceAllocator.{h,cpp}`,
  `SN76489Engine.{h,cpp}`, `SN76489Wrapper.{h,cpp}`,
  `FmRegisterMap.{h,cpp}`, `PatchSystem.{h,cpp}` (extended in Task 09
  with a Tag enum), `PatchBrowser.{h,cpp}`, `Tuning.{h,cpp}`,
  `VgmLogger.{h,cpp}`, `VgmExtract.{h,cpp}`, `Telemetry.{h,cpp}` (with
  scope reduced — see below).
- **apvts collapses** (`01-architecture.md` *Parameter System*): from
  ~300 FM params (6 parts × ~50) to **one** patch's worth (~50 FM
  params), plus the SQ params, plus the D-mode DSP params, plus globals.
  No `_part<n>` suffix anywhere.
- **All v2 apvts params are declared now**, even those whose UI / DSP
  arrives later. Adding them in one pass keeps Tasks 03–10 from each
  reshuffling `createParameterLayout()`. The full list is in §
  *apvts after this task* below.
- **Telemetry scope reduces**: keep level-meter L/R peak + a single
  `noteOn` boolean. Delete the v1 oscilloscope ring buffer, 16-voice
  LED bitmask, and clip flag. The fields can be removed from
  `Telemetry.{h,cpp}`; the methods that wrote them go silent.
- **PluginEditor → placeholder.** Until Task 04 reintroduces the WebView
  with the v2 widget library, `PluginEditor` is either (a) the **WebView
  fallback panel** from `08-ui-views.md` view 10 (native
  `juce::Component`, 1200×560, plain "Gen VST — UI under construction"
  message + retry button), or (b) a one-line "GEN VST" centered placeholder
  matching Task 01-style of the v1 mvp/01 task. Prefer (a) — it's the
  panel Task 04 will also need.
- **Window changes from 960×640 to 1200×560** (ADR-0023). Update the
  editor constructor and any tests that hard-code the v1 dimensions.
- **v1 UI is deleted wholesale**: `ui/src/widgets/`, `ui/src/views/`,
  `ui/src/modals/`, `ui/src/styles/chassis.css`, `styles/sections.css`,
  `styles/modals.css`, `styles/gallery.css`, `binding.js`, `main.js`,
  `gallery.js`, `index.html`, `gallery.html` (ADR-0022). What survives:
  `ui/src/juce/` (JUCE 8 native-interop helper, used in v2 too), the
  new `ui/src/styles/design-system.css` from Task 01, and the
  `ui/mockup-*.html` pages from Task 01.
- **State persistence** (`PluginState.{h,cpp}`) is **stripped to a stub**
  — getStateInformation writes the apvts XML and an empty
  `<GenVstState>` envelope; setStateInformation parses the apvts back.
  The v1 parts array, PSG per-channel MIDI bindings, embedded base64
  PCM, and `.gnbank` references are removed. Custom-root persistence
  and active-patch-path persistence return in Task 10.

### apvts after this task

A single source of truth, no `_part<n>` suffix:

- **Global / mode** — `mode_select` (choice 0=FM/1=SQ/2=D),
  `output_filter` (bool, default true), `ladder_effect` (bool, default
  true), `master_volume` (float).
- **FM (single patch)** — `alg`, `fb`, `lr`, `ams`, `pms`, `lfo_enable`,
  `lfo_rate`; per-op `mul[0..3]`, `dt[0..3]`, `tl[0..3]`, `ks[0..3]`,
  `ar[0..3]`, `dr[0..3]`, `sr[0..3]`, `rr[0..3]`, `sl[0..3]`,
  `ssg[0..3]`, `amon[0..3]`. **v2 additions** (default values from
  `04-patch-system.md` *Defaults on legacy-format load*): `freq_ctrl_mode`
  (choice, default INT_MUL), `retrig_rate` (int 0..1023, default 500),
  `mul_float[0..3]` (float 0.5..15.99), `fixed[0..3]` (bool),
  `freq_fixed_hz[0..3]` (float 20..20000), `mw[0..3]` (float 0..1),
  `vel[0..3]` (float 0..1),
  `note_mode` (choice 0=RETRIG/1=LEGATO, default RETRIG),
  `poly_voices` (int 1..16, default 16), `pitch_bend_range`
  (int 1..12, default 2), `unison_detune_cents` (float 0..50,
  default 0), `hardware_strict` (bool, default false).
- **SQ** — per-channel `psg_atk[0..3]`, `psg_dr1[0..3]`, `psg_sus[0..3]`,
  `psg_dr2[0..3]`, `psg_rr[0..3]`, `psg_vel[0..3]`, `psg_vol[0..3]`,
  `psg_pan[0..3]`, `psg_detune[0..2]` (no detune on noise),
  `psg_glide[0..2]` (no glide on noise), `psg_bend[0..3]` (bool);
  `psg_noise_type` (choice white/periodic), `psg_noise_rate`
  (choice low/mid/high/ch2), `psg_noise_auto` (bool, default false).
- **D** — `prescaler` (float 0..1, default 0), `mono` (bool, default
  false), `dry_wet` (float 0..1, default 1).
- **Settings-bound globals** — `aftertouch_target` (choice off/LFO/TL,
  default LFO), `velocity_to_tl` (bool, default true), `ui_scale`
  (choice 1×/2×/3×), `tooltips_enabled` (bool, default true).

Hardware-range integer ranges go through `juce::AudioParameterChoice` or
`juce::AudioParameterInt`; floats use `juce::AudioParameterFloat`. CC
mappings stay as in `07-feature-spec.md` *MIDI CC Map* and are
reconnected in Task 05 (FM) / Task 06 (SQ).

## Scope

- Delete the C++ files listed above and every reference to them.
- Rewrite `createParameterLayout()` to the v2 apvts listed in
  *apvts after this task* above.
- Rewrite `PluginProcessor::processBlock` so the audio path is:
  - Drain the patch queue (one queue per mode is OK, but a single-typed
    queue with a `Tag` discriminator is also fine — pick one and stick
    with it).
  - Switch on `mode_select`; the three render paths can be stubs for
    now (FM dispatches to the existing `VoiceAllocator`; SQ to the
    existing `SN76489Engine`; D is `buffer.applyGain(0)` until Task 03
    introduces the decimator).
  - Drop per-part MIDI dispatch — every incoming MIDI message goes
    straight to the active engine.
- Delete every v1 UI file under `ui/src/` **except** `juce/`,
  `styles/design-system.css`, and the Task 01 mockup pages.
- Replace `PluginEditor` with the WebView fallback panel from
  `08-ui-views.md` view 10. Size it 1200×560.
- Delete the corresponding tests and update any remaining test that
  still references deleted types (e.g. `PatchLoaderTests` that may
  build a Patch into PartManager — change it to assemble into a local
  `Patch` value).
- Drop `juce_add_binary_data(GenVstWebData …)` / the `GenVstWebBundle`
  custom target from `src/CMakeLists.txt`. The WebView returns in Task 04.

## Out of scope

- The new v2 widgets / views — Task 04 onward.
- The audio input bus + D mode DSP — Task 03.
- `OutputFilter` / `LadderEffect` DSP — Task 03.
- `.psg` / `.gdac` loaders, the tagged preset browser — Task 09.
- Restoring active patch path and custom roots in state — Task 10.

## Implementation steps

1. Delete the C++ files: `PartManager.{h,cpp}`, `DACPlayer.{h,cpp}`,
   `DACKit.{h,cpp}`, `BankIO.{h,cpp}`, `MidiRouter.{h,cpp}` (collapse
   into PluginProcessor as needed). Update `src/CMakeLists.txt`
   `target_sources`.
2. Delete the matching test files; update `tests/CMakeLists.txt`. Keep
   `SmokeTest.cpp`, `PatchLoaderTests.cpp` (adjust to local `Patch`),
   `FrequencyCalcTests.cpp`, `RegisterWriteTests.cpp` (adjust to
   single-`Patch` API), `OpmLoaderTests.cpp`, `Y12LoaderTests.cpp`,
   `VgmExtractTests.cpp`, `VgmLoggerTests.cpp`, `TuningTests.cpp`.
   `VoiceAllocatorTests.cpp` survives but loses any `part` arguments;
   `PsgDacTests.cpp` keeps the PSG half, drops the DAC half;
   `PsgEnvelopeTests.cpp` survives.
3. Rewrite `createParameterLayout()` to the v2 apvts listed above. This
   is a fresh file scope; keep helper lambdas that produce
   parameter-group builders so the diff is readable.
4. Rewrite `PluginProcessor` private members: drop part-arrays,
   `fmRackParams`, `psgRackParams`, `dacRackParams`,
   `polyModeParam`/`monoGlideParam`/`unisonSpreadParam` arrays,
   `previewFifo`, `psgDacParams.dac*`, etc. Keep
   `voiceAllocator`, `psgEngine`, `patchBrowser`, `telemetry`,
   `vgmLogger`. Add a `modeSelectParam` pointer.
5. Rewrite `processBlock` per the *Scope* path above. The render-per-mode
   stubs can be `// TODO Task 03` placeholders.
6. Reduce `Telemetry` to L/R peak + `noteOn` boolean fields. Delete the
   oscilloscope ring buffer, voice mask, clip flag. Update Telemetry's
   internal interface; remove now-dead methods.
7. Rewrite `PluginState`:
   - `getStateInformation` — write the apvts XML inside `<GenVstState>`
     with no extra fields.
   - `setStateInformation` — parse the apvts back; set `stateRestored`
     true; clear any leftover `pendingStateRestore` fields.
   - Delete the v1 base64-PCM, `<parts>`, `<psg>`, `<dac>` handlers.
8. Replace `PluginEditor` with the WebView fallback panel from view 10:
   a 1200×560 native `juce::Component` that paints a dark background
   with centered text "Gen VST — UI under construction" and a small
   "Retry" button (no-op until Task 04). Strip the WebView host code.
9. Delete v1 UI files under `ui/src/` (keep `juce/`, `styles/design-system.css`).
   Delete `ui/index.html`, `ui/gallery.html` (the Vite mockup entries from
   Task 01 stay — they're under `ui/mockup-*.html`).
10. Drop `juce_add_binary_data(GenVstWebData …)` and the
    `GenVstWebBundle` custom target from CMake. The WebView binary-data
    plumbing returns in Task 04.
11. Update the dev-server compile path: `GENVST_DEV_SERVER` flag remains
    but currently goes unused; leave it in place so Task 04 can re-use
    it.

## Deliverables

- C++ deletions: `src/PartManager.*`, `src/DACPlayer.*`, `src/DACKit.*`,
  `src/BankIO.*`, `src/MidiRouter.*` (or collapsed to a tiny shim in
  PluginProcessor).
- C++ rewrites: `src/PluginProcessor.{h,cpp}`, `src/PluginEditor.{h,cpp}`,
  `src/PluginState.{h,cpp}`, `src/Telemetry.{h,cpp}` (scope reduced).
- Test deletions / edits per step 2.
- UI deletions: `ui/src/widgets/`, `ui/src/views/`, `ui/src/modals/`,
  `ui/src/styles/chassis.css` `sections.css` `modals.css` `gallery.css`,
  `ui/src/binding.js`, `main.js`, `gallery.js`, `ui/index.html`,
  `ui/gallery.html`.
- `src/CMakeLists.txt` updated (sources, binary-data removed).
- `tests/CMakeLists.txt` updated.
- The mockup pages and `design-system.css` from Task 01 untouched.

## Verification

1. `cmake --preset windows-debug && cmake --build build/windows-debug`
   builds clean.
2. `ctest --test-dir build/windows-debug --output-on-failure` — every
   surviving test passes.
3. `pluginval --strictness-level 8 --validate "<path>/Gen VST.vst3"` —
   passes.
4. Load the Standalone — window opens at exactly **1200×560** with the
   "UI under construction" fallback. No crash on close.
5. Load the VST3 in Reaper — instantiates as an instrument; opening /
   closing the editor doesn't crash.
6. `grep -rn PartManager src/ tests/` — no matches.
7. `grep -rn DACPlayer src/ tests/` — no matches.
8. `grep -rn DACKit src/ tests/` — no matches.
9. `grep -rn BankIO src/ tests/` — no matches.
10. `grep -rn _part0 src/` — no matches (`_part<n>` suffix retired).
11. The Settings → Preferences in the host shows the new apvts layout:
    a single `alg`/`fb`/`mode_select`/etc. without `_part<n>` suffix
    on any parameter ID.

## Done when

- [ ] Every C++ file listed in *Scope* deletions is gone.
- [ ] `createParameterLayout()` matches the *apvts after this task*
      listing — no `_part<n>` suffixes anywhere.
- [ ] `processBlock` dispatches on `mode_select`; FM and SQ render paths
      still produce sound when driven directly through the engine
      objects (verified via the surviving unit tests).
- [ ] `PluginState` round-trips the apvts; no embedded PCM, no parts
      array.
- [ ] `PluginEditor` is the 1200×560 WebView fallback panel.
- [ ] All v1 UI files are gone from `ui/src/`; mockup files survive.
- [ ] Build is clean, `ctest` is green, `pluginval` passes at strictness
      8, Standalone window is exactly 1200×560.
