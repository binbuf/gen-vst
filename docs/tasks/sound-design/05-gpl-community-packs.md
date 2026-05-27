# Task 05 — Bundle additional GPL / CC community FM banks

> **Milestone:** Bank breadth — one or more additional community FM
> banks ship as named, attributed subfolders alongside the Furnace
> `tfilib` set and the Task 04 originals, giving users hundreds of
> additional patches with clean provenance.
> **Depends on:** `03-patch-taxonomy.md` (the `fm/<category>/`
> structure exists; the CMake glob is recursive; the browser shows
> nested folders).
> **Design references:** `docs/design/04-patch-system.md`
> (*Factory bank*, *Legal Notes*, *Patch Library & Delivery*),
> ADR-0004 (Furnace-only-factory-bank original decision — this task
> revisits its alternatives), ADR-0003 (GPLv3 compatibility).

## Objective

ADR-0004 deliberately limited the factory bank to Furnace's `tfilib`
on the rationale "ship the smallest GPL-clean bank that solves the
zero-presets-on-first-launch problem." That rationale held while the
mvp2 chain was racing toward audible-everywhere; with the chain
shipped, the factory bank is now a product asset rather than a
checkpoint, and bundling additional GPL- or CC-licensed community
banks is high value for low effort.

This task **audits and bundles** one or more additional community FM
banks. Output:

- A short license audit of candidate banks captured in
  `docs/design/04-patch-system.md` *Legal Notes* and (if any bank's
  license needs a one-line acknowledgement to ship) a `NOTICE` or
  `THIRD_PARTY_LICENSES.md` file at the repo root.
- One or more new subfolders under `extern/patches/fm/community/`
  (or under existing category folders if the audited bank ships as
  category-organised already) containing the bundled files.
- An ADR revision: ADR-0004's *Alternatives considered* gains a note
  that the "ship no other community bank" position has been relaxed
  on case-by-case audit, OR a new ADR-0028 supersedes ADR-0004's
  ship-list scope while preserving its "no game-derived patches"
  rule.

## Context & key constraints

### What ADR-0004 actually prohibits

Read in full before authoring. The two prohibitions:

1. **No game-derived patches** — file/directory names encoding game,
   publisher, or character titles. This is a trademark + copyright
   exposure, not a license question. It applies regardless of how
   permissive the upstream license is.
2. **Recursive glob danger** — earlier builds risked enumerating the
   gitignored `extra/` set; Task 03 already resolved this by scoping
   the recursive glob to `extern/patches/fm/` and `extern/patches/sq/`.

What ADR-0004 *does not* prohibit:

- Bundling additional banks whose licenses are GPL- or
  CC0/CC-BY-compatible with the project's GPLv3.
- Adding category-organised banks (the rationale "ship a small set
  to keep the dependency lean" was a checkpoint constraint, not a
  ceiling).

### Candidate banks to audit

The implementer audits each candidate's license, names, and content
before deciding to bundle. The audit is **the work** of this task;
the bundling itself is a directory copy.

Audit checklist per candidate:

- [ ] License is GPLv3-compatible (GPLv2-or-later, GPLv3, LGPL, BSD,
      MIT, CC0, CC-BY all qualify; CC-BY-NC does NOT qualify and is
      rejected; CC-BY-SA only qualifies if it's CC-BY-SA-4.0 with the
      one-way GPL compatibility clause — earlier CC-BY-SA versions
      are not GPL-compatible).
- [ ] No file or folder names contain game, publisher, or character
      titles. (A bank shipped as a community pack with timbre names
      like `Bell.tfi` / `Slap Bass.tfi` is fine; a bank named after
      a specific game's soundtrack is not — even with a permissive
      license, the names themselves are the trademark exposure.)
- [ ] Content is patches, not register dumps from game ROMs. Two
      signals here: the patches' file format (`.tfi` / `.vgi` / `.opm`
      is original-authoring; `.y12` extracted via SMPS tools is
      ROM-derived and is rejected per ADR-0004 even from "free"
      sources), and the bank's documentation (does it cite a game as
      its source?).
- [ ] The bank's upstream is stable enough to vendor — i.e., the
      copyright is clear, the contributors are identifiable enough to
      attribute, and the bank isn't itself an aggregation of other
      banks under unclear licensing.

Plausible candidates (each requires the audit above before
shipping):

1. **Other folders under `tildearrow/furnace/instruments/OPN/`** —
   the Furnace upstream has several community packs beyond `tfilib/`,
   e.g., `Tomato Soup`, individual contributor packs. These ship
   under Furnace's GPL. Each pack needs the file/folder-name audit
   (most are generic timbre names, but the implementer must verify).

2. **TFI / VGI banks bundled with other GPL-licensed Genesis trackers
   and tools** — e.g., **DefleMask community presets**, **VGM Music
   Maker default banks**, **TFM Music Maker bundled instruments** —
   each requires reading the tool's license and the bank's license
   (which may differ). Many of these are CC0 or unspecified-public-
   domain; unspecified-license content is rejected.

3. **Curated free chiptune community packs** distributed via
   chiptune-focused forums (e.g., Furnace's community Discord
   instrument exchanges, Z80/68000 development forums). These often
   have ambiguous licensing and require contacting the author for
   permission. Acceptable if a clear license can be obtained;
   rejected otherwise.

4. **Existing developer-facing Genesis instrument repositories on
   GitHub / GitLab** searchable by `topic:ym2612` or
   `topic:sega-genesis`. Filter to repositories with explicit
   GPL/CC license files; ignore the rest.

The implementer **picks one or two** candidates that pass the audit
cleanly. The goal is breadth (a few hundred additional patches),
not exhaustiveness; shipping one well-audited 200-patch community
pack is better than shipping three sketchy 50-patch banks.

### Where the new bank lives

Three placement options; pick per the audited bank's structure:

**Option A — `fm/community/<bank-name>/<category>/`**: if the bank
is large enough to warrant its own root (e.g., ~150+ patches).
Mirrors how the Task 04 originals live alongside the Furnace
`tfilib` set, but in a `community/` namespace so the source is
visible in the tree.

**Option B — merge into existing `fm/<category>/`**: if the bank
is small (<30 patches) and the licenses are *fully* compatible
with simple in-place file-level attribution (e.g., CC0). Patches
are renamed if necessary to avoid conflicts; no top-level rename.

**Option C — `fm/<bank-name>/<category>/`** (no `community/`
prefix): for a single large attributed pack that's well-known
(e.g., a famous community contributor's bank). Cleaner browse
than Option A; only use if there's exactly one such pack.

Default to Option A. The implementer documents which option was
chosen in the commit message and (if applicable) in the ADR
revision.

### Attribution requirements

- **GPL / LGPL banks**: the source code distribution (i.e., the
  GenVst GitHub repo) must include the bank's full license text. Add
  a copy of the bank's `LICENSE` file at
  `extern/patches/fm/community/<bank-name>/LICENSE` (or alongside
  whatever folder structure is chosen) so the license travels with
  the files. The patches themselves don't need per-file
  attribution embedded (TFI / VGI have no metadata fields for it
  anyway).
- **CC-BY banks**: as above, plus an attribution line in the
  user-facing About dialog and in the project README. Format:
  `<bank name>, by <author/group>, licensed CC-BY 4.0`.
- **CC0 / public-domain banks**: no attribution required, but
  document the source anyway in a comment inside the bank's
  subfolder (a `README.md` next to the patches works) for
  forensic provenance.
- **Furnace upstream packs**: covered by Furnace's existing project-
  wide GPL attribution; the existing ADR-0004 attribution
  requirement satisfies it.

### Bundle size impact

The current factory bank is ~50 files × 42 bytes ≈ 2 KB. Adding a
200-patch community pack adds ~10 KB. The plugin bundle is many MB;
the marginal cost is invisible. There is no size-budget constraint
on this task.

## Scope

### Audit deliverable

A new section in `docs/design/04-patch-system.md` *Legal Notes*
(or a new sibling `04a-factory-bank-audit.md` if the implementer
prefers a separate doc) that records, for each shipped community
bank:

- Bank name
- Author / maintainer / source repository
- License (full SPDX identifier)
- File count
- File name convention (must confirm no game/publisher/character
  references)
- Verification date (when the implementer ran the audit)

### Vendored files

The audited bank(s) committed under `extern/patches/fm/community/<name>/`
(or whichever option from *Where the new bank lives* was chosen),
preserving the bank's own internal structure if it has one.
Re-categorise into the Task 03 taxonomy only if the bank lacks any
internal organisation; do not re-categorise an already-organised
pack (the upstream structure is part of its identity).

### ADR

ADR-0004 either gets a *Status: Amended* note pointing to a new
ADR-0028 ("Additional GPL/CC community banks may be bundled per
case-by-case audit"), or its *Alternatives considered* gets a one-
paragraph note that the alternative was reconsidered and selectively
adopted via this task. The implementer picks the approach that best
matches the project's ADR conventions; if a new ADR is the right
shape, write it. If a note suffices, write it.

### Attribution updates

- `README.md` (repo root) — add a section listing each shipped
  community pack with its license.
- The plugin's About dialog (the modal shown by `mvp2/08`'s settings
  gear) — add a "Patches" section listing the shipped community
  packs with license SPDX IDs.
- If any bank requires CC-BY attribution, add that to the About
  dialog explicitly (CC-BY's clause requires attribution be visible
  to end users, not just in the source repo).

## Out of scope

- Authoring new patches. Task 04 does the original-author work; this
  task only vendors externally-authored content.
- Bundling Y12 / SMPS-extracted patches. ADR-0004 forbids these
  regardless of upstream license; the user's path to game-original
  timbres remains VGM import (Task 06).
- Bundling banks under ambiguous or unspecified licenses. If the
  audit can't pin down the license, the bank is rejected. Don't
  attempt to "infer" a public-domain status from the absence of a
  license file.
- Adding `.opm` files in bulk. OPM is loadable per ADR-0019 but the
  factory bank format is TFI/VGI; if a community pack ships OPM, the
  audit decides whether to (a) ship it as-is, (b) convert to
  TFI/VGI losslessly (DT1 is convertible; DT2 is dropped per
  ADR-0019), or (c) skip it.

## Implementation steps

1. **Survey candidates.** Spend up to half a day collecting candidate
   community banks. For each, capture: URL, license file, file
   structure, rough file count, naming convention. A spreadsheet or
   markdown table is fine; this is research output, not a
   deliverable.
2. **Run the per-candidate audit** against the checklist in
   *Candidate banks to audit*. Reject any that fail.
3. **Pick one or two.** The threshold is: a community pack with
   ≥50 patches, clean GPL/CC license, and timbre-style naming. If
   two banks meet the bar and don't overlap heavily, ship both. If
   only one does, ship one. If none do, mark this task blocked
   pending further surveying — do not ship a marginal pack.
4. **Vendor the picked bank(s).** Commit the files under
   `extern/patches/fm/community/<name>/` with the upstream license
   alongside. If the bank's upstream has a clear version tag,
   capture the version in a `VERSION` file in the bank's folder.
5. **Update CMake** — verify (don't change) that the existing
   recursive `FACTORY_FM` glob from Task 03 picks up the new files.
   If the implementer chose a placement that the glob doesn't cover
   (e.g., a top-level non-`fm/` location), update the glob. (Default
   placement under `fm/community/` is already covered.)
6. **Update attribution** — README.md, About dialog, and any
   per-bank LICENSE / NOTICE files as required by the audit.
7. **Write the ADR (or note).** Capture the decision: which banks
   were audited, which were shipped, why. Reference each bundled
   bank's exact upstream URL + version.
8. **Smoke-test** — build, load the plugin, expand
   `fm/community/<bank>/` in the patch browser, load a few patches
   from each, confirm they play.

## Deliverables

- One or more new subfolders under `extern/patches/fm/` containing
  the audited community bank(s) and their upstream `LICENSE` files.
- Updated `docs/design/04-patch-system.md` *Legal Notes* with the
  per-bank audit table.
- New ADR (e.g., `0028-additional-community-banks.md`) or a note on
  ADR-0004 reflecting the bundling decision.
- Updated repo `README.md` attribution section.
- Updated About dialog attribution section in
  `src/PluginEditor.cpp` or wherever the About modal's text lives.

## Verification

1. **License-text presence.** Every shipped community bank has its
   upstream `LICENSE` file alongside its patches.
2. **Audit table complete.** Every shipped bank has a row in the
   audit table with all six fields populated.
3. **No game-derived names.** A `find extern/patches/fm/community
   -type f` listing has no filename or folder name matching a
   regex of known game titles (the implementer applies a sensible
   heuristic — at minimum, search for common publisher names like
   "sega", "konami", "capcom", "nintendo", and major game series
   names; if any match appears in the new bank, audit-fail the bank
   and pull it).
4. **Build.** `cmake --build build/windows-debug` succeeds; the
   bundle's `Resources/patches/fm/community/<bank>/` mirrors the
   committed structure.
5. **Tests.** `ctest --output-on-failure` is green; all new files
   load via the existing TFI/VGI/OPM loaders.
6. **DAW smoke test.** Load the plugin; expand
   `fm/community/<bank>/` in the patch browser; click through ~20
   patches across the new bank; they load and play without errors.
7. **Pluginval.** `pluginval --strictness-level 8` continues to
   pass.
8. **Attribution visible to end users.** Open the About dialog in
   the running plugin; confirm the new bank's attribution appears
   exactly as the audit specified.

## Done when

- [ ] At least one community FM bank with clean GPL/CC licensing
      and ≥50 patches is committed under
      `extern/patches/fm/community/<name>/`.
- [ ] The bank's upstream LICENSE file lives alongside its patches.
- [ ] The audit table in `04-patch-system.md` *Legal Notes*
      documents every shipped bank.
- [ ] A new ADR or an amendment note on ADR-0004 records the
      decision.
- [ ] `README.md` and About dialog show the new attribution.
- [ ] All new files load via existing loaders; `ctest` and
      `pluginval --strictness-level 8` pass.
- [ ] The audit is also recorded as a *Done date* row so a future
      reviewer can tell at a glance how fresh the audit is. (License
      audits go stale when upstream changes.)
