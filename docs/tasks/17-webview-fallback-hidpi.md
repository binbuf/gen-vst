# Task 17 — WebView fallback panel & HiDPI scaling

> **Depends on:** Task 03.
> **Design references:** `docs/design/08-ui-views.md` (view 9 — *WebView
> fallback panel*), ADR-0016 (fallback), ADR-0017 (HiDPI scaling), ADR-0015,
> ADR-0007.

## Objective

Make the editor robust: show a **native fallback panel** when the WebView
cannot initialise, and implement **HiDPI integer-scale** behaviour so the
pixel-art UI stays crisp on any display.

## Context & key constraints

Both halves can be developed and verified entirely on the Windows dev machine.

### Part A — WebView fallback panel

- If `juce::WebBrowserComponent` fails to initialise — most often a missing or
  broken WebView2 runtime on Windows (ADR-0016) — the editor must show a
  **native `juce::Graphics` panel** (there is no WebView to host HTML), **not**
  a blank window.
- Content per `08-ui-views.md` view 9: "Gen VST — UI unavailable", a short
  explanation, the WebView2 runtime download URL, a `Retry` button, and the note
  that audio keeps working.
- `Retry` attempts to recreate the `WebBrowserComponent` (e.g. after the user
  installs the runtime). On success the editor swaps back to the WebView.
- The **audio processor is unaffected** — only the editor is degraded.
- The panel is sized to the 960×560 editor area; it is plain/functional, not
  pixel-art styled (it is native, not WebView content).

### Part B — HiDPI integer scaling

- The UI offers explicit **1× / 2× / 3×** integer scale presets (ADR-0017). It
  **never scales fractionally** — fractional scaling blurs the pixel art.
- On a display whose effective scale factor is fractional, the UI renders at the
  **nearest integer** scale. On first open, pick the integer preset nearest the
  display's reported scale.
- The user can override via the `UI SCALE` control in Settings (the control was
  built in Task 13 — this task makes it actually change the rendered scale).
- The selected preset is **persisted in plugin state** (Task 16 serializes it —
  ensure the value this task uses is the one Task 16 round-trips).
- The web content is authored at logical 1× (the pixel-art rules); the preset
  is applied as a **whole-window integer zoom**.
- Each WebView engine reports `devicePixelRatio` differently (ADR-0015); apply
  the nearest-integer rule uniformly on top of whatever the backend reports.
- The window base size stays fixed 960×560 (ADR-0007); only discrete integer
  scale presets are offered — no free resizing.

## Scope

- The native WebView fallback panel + WebView init-failure detection + `Retry`.
- HiDPI: nearest-integer scale selection on first open; the `UI SCALE` 1×/2×/3×
  preset applied as a whole-window integer zoom; persistence wiring.

## Out of scope

- The `UI SCALE` control's layout — built in Task 13.
- State serialization mechanics — Task 16 (this task just ensures the scale
  value is exposed for it).
- Free window resizing — post-MVP (ADR-0007).

## Implementation steps

1. Detect `WebBrowserComponent` initialisation failure in the editor; on
   failure, show the native fallback panel instead of the WebView.
2. Implement the fallback panel (native `juce::Component`) with the view 9
   content and a working `Retry`.
3. Implement display-scale detection and the nearest-integer pick on first open.
4. Apply the selected 1×/2×/3× preset as a whole-window integer zoom; wire the
   Settings `UI SCALE` control to change it live.
5. Ensure the chosen scale is exposed to the state layer for persistence.

## Deliverables

`src/WebViewFallbackPanel.{h,cpp}`, updates to `src/PluginEditor.{h,cpp}`,
updates to `ui/src/*` for the integer zoom application.

## Verification

1. **Fallback:** force the WebView init to fail (e.g. temporarily make the
   resource provider / WebView creation fail, or test on a Windows environment
   without the WebView2 runtime) — the editor shows the native fallback panel
   with the message, URL, and `Retry`; **audio keeps playing**. Restore the
   WebView path and confirm `Retry` brings the real UI back.
2. **HiDPI:** on a Windows display set to a fractional scale (e.g. 150%), the
   plugin renders at the nearest integer scale (1× or 2×) and the pixel art
   stays **crisp** — no blur, no fractional sub-pixel edges.
3. The Settings `UI SCALE` control switches 1× / 2× / 3× live; the window
   resizes to the integer multiple of 960×560 and the art stays crisp at each.
4. The chosen scale persists: set 2×, save + reopen the project (with Task 16) —
   it reopens at 2×.
5. `pluginval --strictness-level 8` passes; editor open/close is clean in both
   the WebView and fallback paths.

## Done when

- [ ] A WebView init failure shows the native fallback panel; `Retry` works;
      audio is unaffected.
- [ ] The UI renders at the nearest integer scale on fractional-DPI displays.
- [ ] `UI SCALE` 1×/2×/3× works live and the art stays crisp.
- [ ] The scale choice is persisted.
- [ ] `pluginval` passes.
