# Task 01 — LLM patch generation pipeline

> **Milestone:** All 40 FM originals and all 12 SQ presets generated,
> validated, and ready for human spot-check audition in a single session —
> not 52 individual tuning sessions.
>
> **Depends on:** None for SQ. FM output files require the
> `extern/patches/fm/<category>/` folders from Task 03 to exist before
> committing (the script can write to a staging dir first).
>
> **Accelerates:** Task 04 (FM bank authoring) and Task 02 (SQ retune).
> Those tasks still define the target patch list and verification criteria;
> this task provides the generation tooling so the human role shrinks to
> batch audition and commit, not per-patch authoring.

## Objective

Build `tools/patch-gen/` — a self-contained Python pipeline that uses the
Claude API to generate hardware-valid TFI/VGI (FM) and JSON PSG (SQ) patch
files. The pipeline uses two layers of **premium data** as LLM context:

1. **Category profiles** built from the existing Furnace `tfilib` patches in
   `extern/patches/` — parameter statistics (common ALG values, typical TL
   ranges, AR/DR/SL/RR distributions) that tell Claude "what real Genesis
   patches in this category look like at the register level."

2. **VGM register-log examples** (optional) — user-supplied `.vgm` / `.vgz`
   files from public archives (vgmrips.net, Project2612) are parsed in Python
   using the same register-state tracking logic as `src/VgmExtract.cpp`. Each
   extracted patch becomes a few-shot example with real YM2612 operator values.
   No copyrighted content is bundled; the user supplies and discards the VGM files.

With both context layers, Claude generates patches from synthesis theory
anchored by concrete register data — not generic DX7 advice.

## Files

| File | Purpose |
|------|---------|
| `tools/patch-gen/generate.py` | Main CLI; orchestrates the full pipeline |
| `tools/patch-gen/profile.py` | Reads existing TFI/VGI → per-category stats + example blocks |
| `tools/patch-gen/vgm_examples.py` | Parses VGM/VGZ files → few-shot example blocks |
| `tools/patch-gen/validate.py` | Clamps all parameters to hardware ranges; packs TFI/VGI binary |
| `tools/patch-gen/requirements.txt` | `anthropic>=0.30` |

## Setup

```
pip install -r tools/patch-gen/requirements.txt
export ANTHROPIC_API_KEY=sk-ant-...
```

## Usage

```bash
# Dry-run: preview generated JSON, no API calls, no file writes
python tools/patch-gen/generate.py --mode fm --category bass --dry-run

# Generate all bass FM patches → extern/patches/fm/bass/ (Task 03 dirs must exist)
python tools/patch-gen/generate.py --mode fm --category bass

# One specific patch by stem name
python tools/patch-gen/generate.py --mode fm --patch slap-bass-1

# All 40 FM patches across all categories
python tools/patch-gen/generate.py --mode fm --all

# All 12 SQ presets → extern/patches/sq/
python tools/patch-gen/generate.py --mode sq

# Premium context: add VGM few-shot examples (user-supplied, not committed)
python tools/patch-gen/generate.py --mode fm --category bass \
  --vgm ~/vgm/game1.vgz ~/vgm/game2.vgz

# Use Sonnet instead of Opus for faster/cheaper generation
python tools/patch-gen/generate.py --mode fm --all --model claude-sonnet-4-6

# Write to a staging dir instead of directly into extern/patches/
python tools/patch-gen/generate.py --mode fm --category bass \
  --output-dir /tmp/gen-patches/bass
```

## How the pipeline works

### Phase 1 — Category profile (always active)

`profile.py` scans every `.tfi` and `.vgi` file under `extern/patches/`
(including `extern/patches/fm/<category>/` if Task 03 has run). For each
file it:

1. Parses the binary using the exact same layout as `src/PatchSystem.cpp`
   loaders (`loadTFI`, `loadVGI`).
2. Heuristically classifies the patch into one of the seven categories
   (bass / lead / keys / brass / pad / drums / fx) by filename keyword.
3. Accumulates min/max/median statistics for ALG, FB, carrier TL, modulator
   TL, AR, DR, SL, and RR across the category.

The per-category profile is included in the user turn of every prompt for
that category. The prompt for the next patch in the same category reuses
the same profile text, so Claude's caching keeps the shared context cheap
across a whole category run.

### Phase 2 — VGM few-shot examples (optional, premium)

`vgm_examples.py` implements a Python port of the `VgmExtract.cpp`
register-state tracker. It:

1. Decompresses `.vgz` (gzip) or reads `.vgm` directly.
2. Walks the command stream, applying `0x52`/`0x53` YM2612 register writes to
   a shadow-register per-channel state (mirroring `ChannelState` in
   `VgmExtract.cpp`).
3. On every key-on (`0x28` with operators enabled), snapshots the channel's
   current register state into a `Patch` dict and deduplicates by content hash.
4. Returns up to 10 unique patches as formatted few-shot examples with real
   operator values.

These are **lawful examples**: register logs record chip state, not audio
content or game code. The user downloads VGM files from public archives and
never commits them to the repo.

### Phase 3 — Claude generation loop

For each target patch `generate.py`:

1. Builds the user prompt: `[profile_text] + [vgm_examples if any] + [target description]`.
2. Calls the Claude API with prompt caching on the system prompt (format spec +
   synthesis guide). The shared context across a category run hits the cache
   after the first call, reducing cost substantially.
3. Parses the JSON response.
4. Runs `validate.py` to clamp all values to hardware ranges (mirrors
   `PatchSystem.cpp::clampTo` / `clampSsg`). Out-of-range values are warned
   but the file is still written clamped rather than skipped.
5. Packs the validated dict into a 42-byte TFI or 43-byte VGI binary
   (mirrors `loadTFI` / `loadVGI` layouts exactly) and writes the file.

For SQ presets the output is JSON (the `.psg` schema from
`docs/design/04-patch-system.md`). The existing broken preset content is
included as a "negative example" so Claude knows what to fix.

## Target patch lists

FM targets are the 40 patches from Task 04's *16-bit Genesis idiom* tables:
`bass/` (6), `lead/` (7), `keys/` (6), `brass/` (5), `pad/` (5),
`drums/` (7), `fx/` (4).

SQ targets are the 12 factory presets from Task 02's audit:
`default`, `square-bass`, `pulse-arp`, `soft-lead`, `detuned-chord`,
`bright-pluck`, `retro-beep`, `chip-melody`, `noise-snare`, `periodic-bass`,
`noise-hats`, `title-screen`.

Both lists (with descriptions and known issues for SQ) are hardcoded in
`generate.py` as `FM_TARGETS` and `SQ_TARGETS` dicts so the pipeline is
self-contained.

## Recommended workflow

1. **Run dry-run first** to verify the script starts and Claude returns
   parseable JSON without writing anything:
   ```
   python tools/patch-gen/generate.py --mode fm --category bass --dry-run
   ```

2. **Generate one category**, review the files audibly in the plugin,
   tweak descriptions in `FM_TARGETS` if a patch misses its character,
   then re-run that patch:
   ```
   python tools/patch-gen/generate.py --mode fm --category bass
   # audition in plugin
   python tools/patch-gen/generate.py --mode fm --patch acid-bass
   # re-run one that missed
   ```

3. **Generate all categories** once you're satisfied with the prompt quality:
   ```
   python tools/patch-gen/generate.py --mode fm --all
   ```

4. **Generate SQ** (can run in parallel with FM or independently):
   ```
   python tools/patch-gen/generate.py --mode sq
   ```

5. **Audition and commit** per the Task 02 / Task 04 verification criteria.
   One commit per patch (or per logical pair for drums). Human role here is
   auditory QA, not parameter authoring.

6. **Optional: add VGM premium context** for a second pass on any category
   that produced weak results:
   ```
   python tools/patch-gen/generate.py --mode fm --category brass \
     --vgm ~/vgm/action-game.vgz ~/vgm/rpg.vgz
   ```

## Legal notes

- Generated FM patches (TFI/VGI) are original works: the developer (or LLM
  acting as the developer's synthesis tool) sets every register value guided
  by theory, not by copying from game ROMs. Same legal model as the hand-authored
  originals in Task 04.
- VGM files used as few-shot context are never committed to the repo — the user
  downloads them from public archives (vgmrips.net / Project2612) and uses
  them locally. The generated patches do not reproduce the VGM register values
  directly; they are inspired-by-category examples, not copies.
- The `anthropic` package is a dev dependency for tooling only — not linked
  into the plugin binary, not shipped with the release.

## Verification

1. **Dry-run passes:**
   ```
   python tools/patch-gen/generate.py --mode fm --category bass --dry-run
   python tools/patch-gen/generate.py --mode sq --dry-run
   ```
   Both produce valid JSON, no file writes, exit 0.

2. **Output files parse:** after generation, run
   `ctest -R PatchLoader --output-on-failure` — all generated TFI/VGI files
   must parse without error through the existing C++ loaders.

3. **File sizes correct:** every `.tfi` is 42 bytes, every `.vgi` is 43 bytes.
   ```
   ls -la extern/patches/fm/**/*.tfi | awk '{print $5}' | sort -u
   # should print: 42
   ```

4. **Audible character check:** load plugin, cycle through generated patches,
   spot-check 5–10 per category. Each must match its description in `FM_TARGETS`.
   This is the primary quality gate — automated tests cannot replace it.

5. **No regressions:** `ctest -R PatchLoader` passes including the existing
   Furnace `tfilib` patches (the script never modifies those).

## Done when

- [ ] `generate.py --dry-run` runs without errors (both modes).
- [ ] All 40 FM patches generated and committed under `extern/patches/fm/<cat>/`.
- [ ] All 12 SQ presets generated and committed under `extern/patches/sq/`.
- [ ] Every generated file parses via `ctest -R PatchLoader`.
- [ ] Human audition confirms each patch matches its character description.
- [ ] Existing Furnace patches and original SQ presets still load (no regressions).
