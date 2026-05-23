# Task 29 — VGM logging (Log VGM button)

> **Depends on:** Task 24 (IMPORT-tab button exists as a stub), Task 07 (every
> chip register write goes through `SN76489Wrapper` / `Voice::write`).
> **Design references:** `docs/design/03-psg-synthesis.md`, `docs/design/02-fm-synthesis.md`,
> [VGM file format spec](https://vgmrips.net/wiki/VGM_Specification) — version 1.71.
> **Note:** Stamped by Task 24 when the Log VGM button was stubbed instead of
> implemented. Renumber if a slot conflict arises.

## Objective

Promote the IMPORT-tab **Log VGM** button from a "coming soon" toast (Task 24)
to a working capture. When toggled on, the plugin starts recording every
YM2612 + SN76489 register write to a `.vgm`-format file under
`<userAppData>/GenVst/logs/`. Toggled off → finalise the file, surface the
path via a toast. The same button labels the next click "STOP LOG"
while recording.

## Context & key constraints

- **No new third-party dependency.** Writing VGM means emitting the
  command-stream byte format directly: `0x52 rr dd` per YM2612 port-0 write,
  `0x53 rr dd` per port-1 write, `0x50 dd` per SN76489 write, plus the wait
  commands (`0x61 nn nn`, `0x62`, `0x63`, `0x70`–`0x7F`) and `0x66` to end
  the stream. No upstream VGM library is pulled in.
- **Capture on the audio thread.** The VGM logger lives on the audio thread
  because that is where chip writes happen. It must be lock-free: writes go
  into a SPSC ring buffer; a message-thread Timer drains the ring into the
  open output file at ~10 Hz. The audio thread never touches the filesystem.
- **Sample timing.** VGM wait commands are in 44 100 Hz ticks. Each block
  emits one wait command of `numSamples * 44100 / sampleRate` ticks before
  the block's register writes. Resampling artefacts are acceptable for v1
  (the captured file plays back close to the source rate; sample-accurate
  conversion is post-MVP).
- **File path.** `<userAppData>/GenVst/logs/<UTC-ISO8601>.vgm`. The logs
  folder is auto-created on first capture. A toast shows the absolute path
  when capture stops so the user can find the file.
- **Header.** Emit a VGM 1.71 header: magic `Vgm `, version `0x00000171`,
  YM2612 clock `7670454`, SN76489 clock `3579545`, SN76489 feedback `0x0009`
  + shift register width `16`, data offset `0x40`. Loop offset is zero
  (no loop). Total samples field is back-patched at stop time.

## Scope

- `src/VgmLogger.{h,cpp}` — owns the SPSC ring, the open `juce::FileOutputStream`,
  the `start()` / `stop()` methods. `recordYm2612Write(port, reg, data)` and
  `recordPsgWrite(data)` are the audio-thread entry points.
- `src/Voice.cpp` / `src/SN76489Wrapper.cpp` — call into `VgmLogger` from the
  existing register-write paths when capture is active.
- `src/PluginProcessor.cpp` — own one `VgmLogger`; `prepareToPlay` tells it
  the host sample rate (for wait-tick conversion); `processBlock` flushes the
  pre-block wait into the ring.
- `src/PluginEditor.cpp` — rewrite `toggleVgmLogging` (currently a stub) to
  call `processor.getVgmLogger().toggle()` and surface the resulting state
  via `notify`. The IMPORT-tab JS button's label flips to "STOP LOG" while
  recording so the user has visual feedback.
- `tests/VgmLoggerTests.cpp` — synthetic register writes → file → re-parse
  via the existing `VgmExtract` parser (Task 21) → assert the writes appear
  in the parsed stream.

## Out of scope

- Loop points, GD3 tag block, multi-chip headers beyond YM2612 + SN76489.
- DAC sample stream commands (`0x67` / data blocks) — the DAC's PCM is
  captured indirectly as a sequence of `0x2A` register writes, which is
  enough for round-trip but not space-efficient. Compact DAC encoding is
  post-MVP.
- Real-time playback of a logged file inside the plugin.

## Implementation steps

1. **Scaffold** `VgmLogger.{h,cpp}` with the SPSC ring + the audio-thread
   entry points; add to `src/CMakeLists.txt`.
2. **Hook the chip writes.** Add `VgmLogger*` parameters where Voice /
   SN76489Wrapper write registers, and call into it from those sites.
3. **Header / footer.** On `start()` write the 64-byte VGM header; on
   `stop()` back-patch the total-samples field and close the stream.
4. **Toggle the IMPORT-tab button.** Replace the stub `toggleVgmLogging`
   handler in `PluginEditor.cpp` and emit a notify with the file path on
   stop. Have JS flip the button label between "LOG VGM" and "STOP LOG".
5. **Test.** Write the round-trip test via `VgmExtract::extractFmPatches`.

## Deliverables

- `src/VgmLogger.{h,cpp}` — new.
- Updates to `src/Voice.cpp`, `src/SN76489Wrapper.cpp`,
  `src/PluginProcessor.cpp`, `src/PluginEditor.cpp`, `src/CMakeLists.txt`.
- `tests/VgmLoggerTests.cpp`, `tests/CMakeLists.txt` updated.
- `ui/src/views/fm-view.js` — Log VGM button label toggle.

## Verification

1. `cmake.exe --build build/windows-debug --config Debug` succeeds.
2. `ctest.exe --test-dir build/windows-debug -C Debug --output-on-failure` —
   all green; `VgmLoggerTests` passes.
3. Standalone:
   - Load an FM patch, click **LOG VGM** → label flips to **STOP LOG**.
   - Play a few notes for ~5 s, click **STOP LOG** → toast shows the file path.
   - Open the resulting `.vgm` in vgmplay or Furnace → audio matches what
     was just played.
4. `pluginval --strictness-level 8` — SUCCESS.

## Done when

- [ ] `VgmLogger` writes a valid VGM 1.71 file with YM2612 + SN76489 events.
- [ ] Log VGM button toggles capture and emits the file path on stop.
- [ ] Audio thread never touches the filesystem (writes go through the ring).
- [ ] Round-trip test parses the logged file with `extractFmPatches`.
- [ ] `pluginval` SUCCESS.
