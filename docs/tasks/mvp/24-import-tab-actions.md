# Task 24 — IMPORT tab full action stack

> **Depends on:** Task 22 (rack model). Tasks 21 (Import Bank), 14 (patch browser UI), 16 (state persistence) provide most of the underlying machinery.
> **Design references:** `docs/design/04-patch-system.md`, `docs/design/08-ui-views.md` (view 1 right column, view 4 patch browser).

## Objective

Promote the right column's IMPORT tab to Genny's full action stack: eight buttons (Import Bank, Export Bank, Load State, Save State, Import Instrument, Export Instrument, Log VGM, Import Tuning). Most are thin wrappers over existing functionality; **Log VGM** and **Import Tuning** are new and may stub to a "coming soon" toast with follow-up tasks stamped.

## Context & key constraints

- **Layout matches Genny screenshots** (e.g. `Screenshot 2026-05-23 094533.png`): a vertical button stack in green-LCD bevel style on the IMPORT tab. Button order top-to-bottom mirrors the screenshot.
- **No new modal layers.** Each button either opens a native file chooser (`juce::FileChooser`) or pipes into an existing modal/handler.
- **State files** for Load/Save State write a `.gnvst` file (extension new; format = the `juce::AudioProcessorValueTreeState` XML the plugin already serializes). Document the format in code.
- **Export Bank** writes all currently-loaded patches (every active rack row) as a single JSON bundle to a chosen file. Format: `{ version, rows: [{ type, slot, patchPath, routing }] }`. Importing the bundle restores the same rows by re-loading each `patchPath` and applying `routing`.
- **Log VGM** is a toggle button: starts a `VgmLogger` that captures every YM2612 + SN76489 register write to a `.vgm`-format file in `<userAppData>/GenVst/logs/`. If implementation runs long, **stub the button to a `"Coming soon — see Task 27"` toast** and stamp `docs/tasks/mvp/27-vgm-logging.md`.
- **Import Tuning** opens a `.scl` Scala file picker, parses the tuning, and applies it to MidiRouter's note→frequency table. If implementation runs long, stub to toast + stamp `docs/tasks/mvp/28-scala-tuning.md`.
- **Toast feedback** for every action: success, failure, "nothing loaded" (Save State with empty rack still saves; Export Bank with empty rack toasts and aborts).

## Scope

- UI (`ui/src/views/fm-view.js` or new `ui/src/views/import-tab.js`):
  - 8 buttons in the IMPORT tab body. Use existing button widget styling.
  - Each button calls a native fn; result handled via toast.
- Native fns (`src/PluginEditor.{h,cpp}`):
  - `exportBankDialog()` — save-file dialog; writes the JSON bank.
  - `importBankDialog()` — verify it exists from Task 21; else add — open-file dialog for `.gnb`/`.gnbank`/`.json`; load rows.
  - `saveStateDialog()` — save-file dialog for `.gnvst`; writes `getStateInformation()` XML.
  - `loadStateDialog()` — open-file dialog; calls `setStateInformation`.
  - `importInstrumentDialog()` — call into existing patch browser's Import-file path (already exists in Task 14).
  - `exportInstrumentDialog()` — call into existing patch browser's Export path.
  - `toggleVgmLogging()` — if implemented: start/stop VgmLogger and toast file path. Else stub.
  - `importTuningDialog()` — if implemented: parse `.scl`, apply. Else stub.
- New `src/BankIO/BankIO.{h,cpp}` — JSON read/write of the bank bundle. Use `juce::JSON`.
- `tests/BankIOTests.cpp` — roundtrip bank export/import; nothing-loaded edge case.

## Out of scope

- A dedicated bank-editor UI (the bank IS the current rack state).
- Encrypted/signed state files.
- Multi-version bank format migration (v1 only).
- Tuning UI editor (only file import).

## Implementation steps

1. **IMPORT tab UI** — lay out the 8 buttons; wire each to a native fn name.
2. **`BankIO`** — implement JSON bundle write + read. The "row" payload mirrors Task 22's `getRackState()` shape so import is `for row in bundle.rows: addInstrument(row.type) ; loadPatch(row.patchPath) ; applyRouting(row.routing)`.
3. **Native fns** — implement the 6 always-on actions (Import/Export Bank, Load/Save State, Import/Export Instrument). Each uses `juce::FileChooser` async API on the message thread.
4. **VGM Log & Tuning** — implement OR stub. If stubbed, write the two follow-up task files (`27-vgm-logging.md`, `28-scala-tuning.md`) with the same task-file structure as Task 21.
5. **Toasts** — reuse the existing notification channel for all outcomes.
6. **Tests** — `BankIOTests` for JSON roundtrip + empty-rack edge case.

## Deliverables

- `ui/src/views/fm-view.js` (or new `import-tab.js`) — button stack.
- `src/PluginEditor.{h,cpp}` — six new native fns (plus stubs for the other two).
- `src/BankIO/BankIO.{h,cpp}` — new.
- `tests/BankIOTests.cpp` — new.
- `src/CMakeLists.txt`, `tests/CMakeLists.txt` — updated.
- `docs/tasks/mvp/27-vgm-logging.md`, `docs/tasks/mvp/28-scala-tuning.md` — if stubbed.
- `docs/design/04-patch-system.md` — append a "Bank bundle format" subsection.

## Verification

1. `cmake.exe --build build/windows-debug --config Debug` succeeds.
2. `ctest.exe --test-dir build/windows-debug -C Debug --output-on-failure` — all green; `BankIOTests` passes.
3. Standalone:
   - Load 2 FM + 1 SQ + 1 D into the rack → `Export Bank` → pick a `.gnbank` file → toast confirms write.
   - Clear rack → `Import Bank` → pick the file → rack rebuilds with the 4 rows in original order.
   - `Save State` → pick a `.gnvst` → toast. `Load State` after a reset → state restored.
   - `Import Instrument` → loads a single `.tfi` via the existing file-picker path.
   - `Export Instrument` → with an FM row selected, writes a `.tfi`.
   - `Log VGM` → either records or toasts `"Coming soon — see Task 27"`.
   - `Import Tuning` → either applies a `.scl` or toasts `"Coming soon — see Task 28"`.
4. `pluginval --strictness-level 8` — SUCCESS.

## Done when

- [ ] IMPORT tab shows 8 buttons in Genny's order.
- [ ] Import/Export Bank roundtrips a multi-instrument rack.
- [ ] Load/Save State roundtrips full plugin state.
- [ ] Import/Export Instrument call through to the patch-browser paths.
- [ ] Log VGM + Import Tuning either work or stub cleanly with follow-up task files stamped.
- [ ] `BankIOTests` covers the JSON roundtrip.
- [ ] `pluginval` SUCCESS.
