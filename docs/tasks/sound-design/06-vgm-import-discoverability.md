# Task 06 — VGM Import discoverability

> **Milestone:** Game-original path is discoverable — users learn that
> they can extract patches from any Genesis game's audio via the
> lawful VGM-from-public-archives workflow, without trial-and-error or
> reading the source code.
> **Depends on:** `mvp2/09` (preset browser shipped with toolbar
> including a generic "Import file" button), `mvp2/11` (toast
> notifier in place for VGM extraction summaries).
> **Design references:** `docs/design/04-patch-system.md` (*VGM Bank
> Import*, *Folders, Import & Export*), ADR-0019 (one-click Import
> Bank UX), `docs/tasks/sound-design/README.md` (*Legal framing*).

## Objective

VGM extraction is the **lawful path** to game-original FM timbres:
the user supplies a `.vgm` / `.vgz` file from a public archive
(vgmrips.net, Project2612), the plugin walks the register-write
stream, and patches are written to the user-imported root. ADR-0019
specifies this as a one-click flow via a dedicated "Import Bank"
button.

Current state (audit before starting this task — may have evolved):

- **VGM extraction works via drag-and-drop only.** Dropping a
  `.vgm` / `.vgz` onto the plugin window invokes `extractFmPatches`
  and writes the extracted patches to the user-imported root with a
  summary toast. The code is in `src/PluginEditor.cpp` around line
  757 (`if (extLower == ".vgm" || extLower == ".vgz") …`).
- **No dedicated "Import Bank" button exists.** The preset browser's
  toolbar has only an "Import file" button which routes through
  `importPatch` (single-file copy, no VGM extraction). ADR-0019's
  specified button was deferred during `mvp2/09` implementation and
  not re-added in any subsequent task.
- **No discoverability surface.** Drag-and-drop is invisible until
  someone attempts it. The About dialog and README don't mention
  VGM extraction at all. A new user has no path to learn this
  exists.

This task closes the gap by:

1. Adding the dedicated **Import Bank** button to the preset
   browser toolbar (matches ADR-0019; not a UX change, just shipping
   what the ADR specified).
2. Adding an empty-state hint inside the preset browser's right pane
   when the user is browsing an empty folder or the user-imported
   root for the first time.
3. Updating the About dialog and `README.md` to call out the VGM
   workflow as the lawful path to game-original patches.

## Context & key constraints

### What's lawful and what isn't

Repeating the *Legal framing* from this chain's README so the task
is self-contained:

- **Lawful**: the user obtains a `.vgm` / `.vgz` file from a public
  archive (vgmrips.net, Project2612, the VGM community's published
  collections) and drops it into the plugin (or clicks Import
  Bank). The file is a register-write log; the extraction walks
  that log and snapshots each unique FM channel state at key-on.
  No ROM content is read or shipped.
- **Unlawful** (not enabled by this plugin): the user extracts
  patches from a Genesis ROM via SMPS-style tooling. This produces
  `.y12` files. ADR-0004 forbids shipping `.y12` as factory
  content but does not forbid the user loading their own; the
  plugin's loader supports `.y12` for that flow. **This task does
  not surface .y12 as part of the discoverability messaging** — the
  factory bank stays VGM-positive, not ROM-extraction-positive.
- **Edge case**: a `.vgm` file is itself a derivative of game audio,
  so distributing one is the archive maintainer's call. Mainstream
  archives operate under a longstanding fan-community convention; we
  link to vgmrips.net as the canonical source without making any
  warranty about specific files' status.

### Existing code surfaces touched

- **`src/PluginEditor.cpp`** — currently handles VGM extraction in
  the drag-and-drop callback (lines ~757–772). The new Import Bank
  button calls into the same `extractFmPatches` +
  `saveExtractedPatches` path; the dnd handler stays as-is so users
  can keep dropping files.
- **`src/PluginEditor.h`** — gains a new native function exposed to
  the WebView, e.g., `importBank` that takes a file path and runs
  the extraction. (Or the existing `importPatch` is extended with a
  mode flag — implementer's call; the simpler choice is a separate
  native function.)
- **`ui/src/modals/preset-browser.js`** — adds an `Import bank…`
  button next to the existing `Import file` button. Calls the new
  native function. On success, the toast (already wired by
  `mvp2/11`'s notifier) shows the summary. The browser refreshes
  its writable-root mtime poll and the new patches appear.
- **`ui/src/modals/about.js`** — adds a new attribution row and a
  one-line tip about VGM extraction.
- **`README.md`** (repo root) — gains a *Getting game-original
  patches* section linking to vgmrips.net and explaining the
  workflow.

### Empty-state hint placement

The preset browser's right pane currently shows an empty list when:

- The user clicks a folder that contains no supported patches.
- The user-imported root is empty (first run).

For both empty conditions, add an inline help block (not a modal,
not a toast — those are too heavy) with text approximately:

> No patches in this folder.
>
> **Looking for game-original sounds?** Click **Import bank…** above
> and choose a `.vgm` or `.vgz` file. Public archives like
> [vgmrips.net](https://vgmrips.net) collect register logs of Genesis
> games' audio that you can use to extract their FM patches.

The exact copy can be shortened; the implementer tunes the wording
during the UI implementation pass. The key elements are: (a) a
sentence acknowledging the empty state, (b) the call-to-action
verb tied to the new button, (c) the vgmrips.net link, (d) a
one-sentence explanation of what VGM extraction does.

### Button placement and copy

In the preset browser toolbar (currently
`[+ Add Folder…] [Import file] [Export▾] [Delete]`), insert the new
button between `Import file` and `Export▾`:

```
[+ Add Folder…] [Import file] [Import bank…] [Export▾] [Delete]
```

`Import bank…` (with the ellipsis) signals "this opens a file
picker" — matches the convention `Add Folder…` already follows.
The icon (if the UI uses one) is the same generic file-import icon
as `Import file`; no need for a special VGM-shaped icon.

### File picker filter

The native file picker uses the filter `*.vgm;*.vgz` exclusively —
ADR-0019's "single dialog" requirement. Don't widen it to other
extensions; users with single `.tfi` / `.vgi` files use `Import file`.

### Toast copy

Existing copy in `PluginEditor.cpp` after a successful drag-drop
extraction:

> Imported N bank patch(es)

Keep this exactly for the Import bank button path — same code path,
same toast. If the implementer adds telemetry / logging for the new
button, label the event source so drop-vs-button can be
distinguished in metrics (post-MVP analytics work, optional).

### About dialog row

The existing `ATTRIBUTIONS` table in `ui/src/modals/about.js`
already has a row for "DMP PSG community presets (user-imported)".
Add a sibling row:

```js
["VGM register-log imports (user-supplied)",
 "Varies per file — register logs from public archives",
 "Import-only path; not bundled; user responsibility. See README for vgmrips.net workflow"],
```

The third column already establishes the "not bundled; user
responsibility" pattern for `.dmp` imports, so the new row follows
the existing voice.

If practical, the implementer can also add a *Tip* line near the
bottom of the About modal body (above the source-code link) reading
e.g.: "Want game-original patches? Use **Import bank…** with a
.vgm from vgmrips.net." This is optional polish; the attribution
table row is the required deliverable.

### README section

Add a section to the repo's top-level `README.md`. Suggested
placement: after the introductory paragraphs / install instructions,
before the build instructions section. Suggested heading:
**Getting game-original patches**. Copy approximately:

> Gen VST's factory bank ships an original Genesis-idiom collection
> plus the GPL Furnace `tfilib` community bank — see
> `docs/design/04-patch-system.md` for the bundled licenses.
>
> For patches lifted from actual Genesis games, use the **Import
> bank…** button in the preset browser (or drop a file onto the
> plugin window). It accepts `.vgm` and `.vgz` register-log files
> from public archives like [vgmrips.net](https://vgmrips.net) and
> [Project2612](https://project2612.org). The plugin walks the log,
> extracts every unique FM channel state at each key-on event, and
> writes them as `.tfi` files into your user-imported patch folder
> — ready to play.
>
> This workflow uses only register logs from public archives; no
> game ROM content is read or shipped.

Adjust wording for the implementer's voice; keep the **non-ROM,
public-archive** framing explicit so the boundary is documented.

## Scope

### C++ changes

- `src/PluginEditor.h` — declare a new native callback for the
  Import bank flow (e.g., `juce::var importBank (const juce::var&
  args)`).
- `src/PluginEditor.cpp` — implement the new callback. It opens a
  native `juce::FileChooser` with filter `"*.vgm;*.vgz"`, runs
  `extractFmPatches + saveExtractedPatches` on the picked file
  (reuse the existing logic from the drag-and-drop path; consider
  extracting the shared body into a private helper to avoid
  duplication), and emits the existing summary toast on completion.
  Register the callback with the WebView's native function bridge
  next to `importPatch`.

### UI changes

- `ui/src/modals/preset-browser.js`:
  - Add an `Import bank…` button in the toolbar between
    `Import file` and `Export▾`.
  - Wire it to the new `importBank` native function.
  - Add an empty-state hint inside the right pane's list rendering
    path that fires when the visible patch list is empty. The hint
    contains a one-line ask + the vgmrips.net link.
  - Optional polish: animate the hint in on first render so it's
    not distracting after the user has loaded patches.
- `ui/src/modals/about.js`:
  - Add a new row to `ATTRIBUTIONS` for VGM register-log imports
    (see *About dialog row* above).
  - (Optional) Add a one-line *Tip* near the bottom of the modal
    body about Import bank…

### Documentation

- `README.md` — new section "Getting game-original patches" with
  the workflow + vgmrips.net link.

## Out of scope

- Bundling any `.vgm` / `.vgz` files with the plugin. ADR-0004's
  ban on game-derived factory content still applies; we link to
  public archives, not vendor archive contents.
- Building a multi-step VGM extraction wizard (channel selection,
  preview, time-scrub). ADR-0019 rejected this; the one-click flow
  is the contract.
- A per-`.vgm` license/provenance dialog. Whether a given archive
  file is appropriate to use is the archive's and the user's
  responsibility; the plugin's UI surfaces the workflow neutrally.
- Surfacing the `.y12` (ROM-extracted) format as part of the
  discoverability messaging. The plugin supports the format for
  user files (ADR-0019); we don't promote ROM extraction.
- Telemetry / analytics on Import bank usage.

## Implementation steps

1. **Re-confirm the audit.** Run the plugin in a host, attempt VGM
   drag-and-drop, confirm the existing flow works. Confirm there's
   no Import bank button in the browser toolbar. If the audit
   contradicts this task's premise (e.g., the button was added in
   some other recent task), adjust scope before implementing.
2. **Add the C++ native callback.** Implement `importBank` in
   `PluginEditor.{h,cpp}`. Pull the shared VGM extraction body out
   of the drag-and-drop handler into a private helper used by both
   paths to avoid duplication.
3. **Add the UI button.** Wire `Import bank…` into the preset
   browser toolbar; bind it to the new native function. Confirm
   the file picker opens with the right filter.
4. **Add the empty-state hint.** Detect the empty-list condition
   in the browser's right-pane render path and inject the hint
   block. Test with: (a) a folder that contains no patches, (b)
   the user-imported root on a fresh install.
5. **Update the About dialog.** Add the new attribution row;
   optionally add the tip line.
6. **Update the README.** Add the *Getting game-original patches*
   section.
7. **Smoke test end-to-end.** Click Import bank…, pick a `.vgm`
   file from vgmrips.net (any small game's log is fine), confirm
   extraction succeeds and patches appear in the user-imported
   root. Confirm the toast fires. Confirm the empty-state hint
   shows when expected.

## Deliverables

- `src/PluginEditor.{h,cpp}` — new `importBank` native callback;
  shared extraction helper used by both Import bank and
  drag-and-drop paths.
- `ui/src/modals/preset-browser.js` — `Import bank…` toolbar
  button; empty-state hint.
- `ui/src/modals/about.js` — new attribution row; optional tip.
- `README.md` — new *Getting game-original patches* section.

## Verification

1. **Button shows.** Open the preset browser; the toolbar shows
   `[+ Add Folder…] [Import file] [Import bank…] [Export▾]
   [Delete]` in that order.
2. **Button works.** Click `Import bank…`; native file picker
   opens with `*.vgm;*.vgz` filter; pick a real `.vgm` file from a
   public archive; the extraction runs and writes patches into the
   user-imported root; the existing summary toast fires
   ("Imported N bank patch(es)"); the patches appear in the browser
   under `Imported` after the mtime poll cycle.
3. **Drag-and-drop still works.** Drop a `.vgm` file onto the
   plugin window; same extraction and toast. (The button is
   additive, not a replacement.)
4. **Empty-state hint shows.** Navigate to an empty folder (or the
   user-imported root on a fresh install / after deleting all
   imported patches); the right pane shows the empty-state hint
   with the vgmrips.net link. The link is clickable and opens in
   the system browser via the WebView's default link behaviour.
5. **About dialog updated.** Open the About modal from the header
   wordmark or the Settings modal; the new attribution row appears
   in the table; the optional tip (if added) reads sensibly.
6. **README updated.** The repo's `README.md` shows the new
   *Getting game-original patches* section in a sensible position;
   `mdformat` / markdown render preview the section without
   layout errors.
7. **Tests still pass.** `ctest --output-on-failure` is green —
   this task adds no new tests (it's UI/copy), but the existing
   suite continues to pass.
8. **Pluginval.** `pluginval --strictness-level 8` continues to
   pass.

## Done when

- [ ] Import bank… button exists in the preset browser toolbar and
      runs the same extraction path as drag-and-drop.
- [ ] Empty-state hint shows in the right pane when the visible
      patch list is empty.
- [ ] About dialog includes the VGM import attribution row.
- [ ] README includes the *Getting game-original patches* section
      with the vgmrips.net link.
- [ ] Drag-and-drop continues to work for VGM and other file types.
- [ ] `ctest` + `pluginval --strictness-level 8` are green.
