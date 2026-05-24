# Task 12 — Telemetry: oscilloscope, VU & voice/clip LEDs

> **Depends on:** Task 06, Task 11.
> **Design references:** `docs/design/05-ui-ux.md` (primary — *C++ → JS
> telemetry push*), `docs/design/08-ui-views.md` (view 1 — *Header meter bay*),
> ADR-0010.

## Objective

Feed the header's meter widgets with **live audio telemetry**: the
oscilloscope, the VU meter, the 16 voice-activity LEDs, and the clip LED.
Replace the static placeholders from Task 11 with real data pushed from the
audio engine.

## Context & key constraints

- **Telemetry is processor-owned** so the editor can be opened/closed
  independently of audio (`05-ui-ux.md` *C++ → JS telemetry push*).
- The audio thread writes telemetry **lock-free**:
  - Oscilloscope samples → a single-producer/single-consumer ring buffer
    (`juce::AbstractFifo`).
  - VU levels, the clip flag, and the per-voice key-on mask → `std::atomic`
    scalars. The voice mask has **16 bits**, one per pool voice (ADR-0010).
- A `juce::Timer` in the editor at **~30 Hz** reads the telemetry on the message
  thread, downsamples the scope buffer to **~768 points**, and pushes **one
  combined event**:
  `emitEventIfBrowserIsVisible("meterData", { scope, vuL, vuR, clip, voiceMask })`.
- The JS side subscribes with `addEventListener("meterData", …)` and repaints
  the four widgets.
- The **clip LED** lights from the `clip` flag and decays over ~1 s.
- No new audio-thread cost beyond cheap lock-free writes — keep `processBlock`
  allocation- and lock-free.

## Scope

- C++ telemetry storage on the processor: the scope ring buffer + the atomic
  VU/clip/voiceMask scalars; the audio thread populates them each block.
- The editor's ~30 Hz `juce::Timer`, the scope downsample, and the single
  `meterData` push.
- The JS `oscilloscope` and `vu-meter` widgets (now live) and the
  voice-activity LED row + clip LED, all driven by `meterData`.

## Out of scope

- Any audio-engine change beyond writing telemetry.
- The `notify` event channel → Task 13.

## Implementation steps

1. Add the telemetry storage to the processor; write it from `processBlock`
   (scope samples to the FIFO; VU/clip/voiceMask atomics).
2. Add the editor `juce::Timer`; read telemetry, downsample the scope, push the
   combined `meterData` event.
3. Implement the JS `oscilloscope` and `vu-meter` widgets and the voice-LED row
   + clip LED; subscribe to `meterData`.

## Deliverables

`src/Telemetry.{h,cpp}` (or telemetry members on `PluginProcessor`), updates to
`src/PluginProcessor.{h,cpp}` and `src/PluginEditor.{h,cpp}`,
`ui/src/widgets/oscilloscope.*`, `ui/src/widgets/vu-meter.*`, the voice/clip LED
JS, updates to `ui/src/views/fm-view.*`.

## Verification

1. Build + Standalone. Play notes: the **oscilloscope** draws the output
   waveform in real time; the **VU meter** tracks the output level (rises with
   louder playing, falls when silent).
2. **Voice LEDs:** play one note → exactly one LED lights; play a 5-note chord →
   five LEDs; release → they go dark. Hold 16 notes → all 16 light.
3. **Clip LED:** drive the output into clipping (max master gain + dense chord)
   → the clip LED lights and then decays over ~1 s after the overload stops.
4. Close and reopen the editor while audio is playing — no crash; telemetry
   resumes; the timer does not run when no editor is open.
5. `pluginval --strictness-level 8` passes — `processBlock` stays lock-free.

## Done when

- [ ] Audio-thread telemetry is written lock-free (FIFO + atomics).
- [ ] The editor pushes one `meterData` event at ~30 Hz.
- [ ] Oscilloscope and VU are live; 16 voice LEDs reflect key-on state; the clip
      LED lights and decays.
- [ ] Editor open/close during playback is clean; `pluginval` passes.
