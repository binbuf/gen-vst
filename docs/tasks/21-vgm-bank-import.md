# Task 21 — VGM bank import ("Import Bank")

> **Depends on:** Task 09, Task 20.
> **Design references:** `docs/design/04-patch-system.md` (primary — *VGM Bank
> Import*, *Folders, Import & Export*), `docs/design/08-ui-views.md` (view 4
> *Patch browser*, view 11 *Native file choosers*), ADR-0019.
> **Memory:** [[reference-genny-vst-features]] — Genny's one-click Import Bank
> UX is the parity bar.

## Objective

Add **Import Bank** — a single button on the IMPORT tab that picks a `.vgm`
or `.vgz` file, extracts every unique FM channel state captured at each
key-on event, writes each state to disk as a `.tfi`, and surfaces the count
via a toast. The flow is **one click** — no second confirmation dialog, no
per-patch preview modal — matching Genny VST's "Import Bank" behavior.

## Context & key constraints

- **VGM is a register-write log**, not a list of patches. A "patch" emerges by
  shadow-tracking the FM register state per channel and snapshotting on each
  key-on (register `0x28` write whose data has any of the top four bits set).
- **`.vgz` is gzipped VGM.** Decompress in memory via
  `juce::GZIPDecompressorInputStream`. **No new third-party dependency.** The
  existing libvgm submodule (ADR-0009) is **not** expanded to cover VGM
  parsing — its scope stays "SN76489 emulation core only".
- **Parser scope** (per `04-patch-system.md` *VGM Bank Import → Parser scope*):
  - `0x52 rr dd` — YM2612 port 0 register write (apply to shadow state).
  - `0x53 rr dd` — YM2612 port 1 register write (CH4-6 registers).
  - `0x61 nn nn` — wait N samples; `0x62` — 735; `0x63` — 882; `0x70`–`0x7F` —
    (n+1) samples. The parser advances its sample clock but the clock is only
    used for state tracking; bank import does not play the file.
  - `0x66` — end of sound data; stop.
  - Any other byte is a command with a known length (per the VGM spec's
    per-command length table) — skip its bytes correctly but do nothing.
- **Dedup by content hash.** Multiple key-ons with identical register state
  collapse to one patch. Hash any stable function of the byte representation
  (e.g., `std::hash` over a packed array of the relevant register bytes); the
  exact hash is implementation detail as long as identical state collapses.
- **Patch naming:** `"<filename-stem> #<n>"` where `n` starts at 1, in the order
  patches were first observed.
- **Background-thread extraction.** The user clicks Import Bank and the audio
  thread / message thread must remain responsive while a multi-megabyte VGM
  parses. Run `extractFmPatches` on a `std::thread` or `juce::Thread`; post the
  completion message back to the message thread via the same relay channel
  used by Task 10 widgets.
- **UI flow** (per ADR-0019 and `04-patch-system.md` *VGM Bank Import → UX*):
  one button, one file picker, one toast. No second modal. No per-patch
  selection. The IMPORT list refreshes from the imported root after the writes
  complete.
- **Errors** (malformed header, no YM2612 in the file, zero key-ons found,
  write failure for any output `.tfi`) surface via the existing WebView
  notification toast (Task 13). The IMPORT list is left unchanged on error.
- **Drag-and-drop:** a `.vgm` or `.vgz` dropped onto the plugin window runs
  the same extraction path as the button. Implemented in
  `juce::FileDragAndDropTarget::filesDropped` by branching on extension.

## Scope

- `src/VgmExtract/VgmExtract.{h,cpp}` — minimal VGM 1.50+ parser, per-channel
  shadow register state, key-on snapshot + content-hash dedup, public
  `extractFmPatches(path, &error) → std::vector<Patch>`.
- `tests/VgmExtractTests.cpp` — synthetic VGM byte buffers exercising each
  branch (single key-on, repeated identical key-on, multi-channel, `.vgz`
  round-trip, malformed header, no-YM2612 file).
- Wire `Import Bank` button into the IMPORT tab (`ui/src/views/fm-view.*` or
  the existing IMPORT-tab component).
- JS↔C++ bridge — one new relay `vgm-import-request` (JS→C++ with
  `{ filePath }`) and one response `vgm-import-complete` (C++→JS with
  `{ savedCount, error? }`).
- C++ side: `juce::FileChooser` for the `*.vgm;*.vgz` filter; background
  extraction via `juce::Thread` (or equivalent); per-patch `exportTFI` to
  `<userAppData>/GenVst/patches/imported/`; final post back to the IMPORT-tab
  refresh path from Task 14.
- Drag-and-drop branch in `juce::FileDragAndDropTarget::filesDropped` for
  `.vgm` / `.vgz` runs the same extraction path.

## Out of scope

- A timeline-scrub UI for VGM (per-channel preview, pick-and-save) — explicitly
  rejected in ADR-0019.
- Bank export back to `.vgm` (out of MVP scope).
- Multi-instrument OPM bank import (Task 20 post-MVP backlog).
- Hardware-accurate VGM playback (`extractFmPatches` is not a player — sample
  timing only matters insofar as it advances the cursor; it does not synthesize
  audio).

## Implementation steps

1. Scaffold `src/VgmExtract/VgmExtract.{h,cpp}` with the public
   `extractFmPatches` signature; add to `src/CMakeLists.txt`.
2. Implement the VGM header parse: magic `Vgm `, version (≥ 1.50), data offset.
   Detect `.vgz` by extension; decompress via
   `juce::GZIPDecompressorInputStream` into a `juce::MemoryBlock` before
   parsing. Reject files without YM2612 (header field at offset `0x2C`).
3. Implement the command-stream walker: handle `0x52` / `0x53` writes (update
   per-channel shadow state); the wait commands listed above (advance sample
   clock); `0x66` (stop); everything else (skip with the correct length).
4. On every `0x28` write whose data has any of the top four key-on bits set,
   assemble a `Patch` from the targeted channel's shadow state, hash, and
   append to the result if new.
5. Add `tests/VgmExtractTests.cpp` with the test cases listed in the
   verification section. Inline byte-buffer fixtures keep tests
   self-contained.
6. Wire the Import Bank button in the IMPORT tab. Add the JS-side relay calls
   and toast surfacing.
7. C++ editor side: handle `vgm-import-request` → run `juce::FileChooser` on
   the message thread → spawn a `juce::Thread` to run `extractFmPatches` →
   on completion (`juce::MessageManager::callAsync`) write each patch via
   `exportTFI` to the imported root → trigger the IMPORT-tab refresh from
   Task 14 → reply `vgm-import-complete { savedCount, error? }`.
8. Extend `juce::FileDragAndDropTarget::filesDropped` (added in Task 14) to
   branch on `.vgm`/`.vgz` extension into the same extraction path.

## Deliverables

- `src/VgmExtract/VgmExtract.{h,cpp}`
- `tests/VgmExtractTests.cpp`
- `src/CMakeLists.txt` — new module sources
- `tests/CMakeLists.txt` — new test file
- `ui/src/views/<import-tab>.*` — Import Bank button + relay
- `src/PluginEditor.{h,cpp}` — `vgm-import-request` handler, background
  extraction, `.vgm`/`.vgz` drag-and-drop branch
- Toast strings via the existing notification channel

## Verification

1. `ctest --test-dir build/windows-debug --output-on-failure` — `VgmExtractTests`
   passes, covering:
   - **Single key-on:** one channel, one register sequence, one key-on → 1
     patch extracted.
   - **Dedup:** same channel, repeated identical key-on with no register
     changes between → still 1 patch.
   - **Multi-channel:** different patches on channels 1, 2, 3 → 3 patches,
     in channel/order observed.
   - **VGZ round-trip:** a synthetic VGM gzipped to a `.vgz` produces the same
     patch list as the uncompressed equivalent.
   - **Malformed header:** wrong magic / unsupported version / missing
     YM2612 chip → empty result and a descriptive error.
   - **No key-ons:** a file with YM2612 writes but no `0x28` key-on → empty
     result, descriptive error ("no FM patches found").
2. Standalone E2E:
   - Click Import Bank → file picker → pick `.vgm` → toast shows
     `"Imported N patches from <filename>"` → IMPORT list refreshes with
     `N` new entries → click one → plays expected timbre.
   - Same with a `.vgz`.
   - Drag a `.vgm` onto the plugin window → same extraction path runs.
   - Drag a malformed file → toast surfaces the parser error; IMPORT list is
     unchanged.
3. `pluginval --strictness-level 8` passes.
4. Long-file responsiveness: import a multi-megabyte VGM (e.g. a 5-minute
   Sonic 2 rip if available) → UI stays responsive during extraction →
   completion toast fires within a reasonable window.

## Done when

- [ ] `extractFmPatches` parses `.vgm` and `.vgz`, ignores non-YM2612 writes,
      dedupes by content hash, and returns descriptive errors on malformed
      input.
- [ ] `VgmExtractTests` covers single/repeat/multi-channel/`.vgz`/malformed/
      no-key-ons.
- [ ] Import Bank button is one-click: file picker → toast → IMPORT list
      refreshes. No second dialog.
- [ ] Background extraction keeps the UI responsive on multi-MB files.
- [ ] Drag-and-drop of `.vgm`/`.vgz` runs the same path.
- [ ] `pluginval` passes.
