# Gen VST — Sound Design Task Chain

A focused chain of 5 tasks that improves the **out-of-the-box patch
experience** without changing any DSP code. The mvp2 chain
(`docs/tasks/mvp2/`) made every mode audible and the preset browser
work; this chain makes what users *hear on first launch* feel like a
Genesis-era sound engine rather than a generic OPN library.

## Scope

These tasks touch only:

- `extern/patches/` — file authoring + reorganisation.
- `src/CMakeLists.txt` — factory-patch staging globs and bundle copy.
- `src/PatchBrowser.{h,cpp}` and `ui/` widgets — surface VGM Import as
  the lawful path to game-original timbres.

No engine code, no patch format changes, no ADRs required. Each task
ships an audible improvement; the four sound-authoring tasks
(`01`, `03`, `04`) finish with a **by-ear tuning pass against the live
plugin** — the JSON / TFI starting values in those tasks are seeds,
not final values.

## Legal framing (carries through every task)

Three legal paths exist for game-feel content; this chain uses all
three:

| Path | What ships | Authority |
|------|------------|-----------|
| **Original works** authored "in the style of" Genesis sound design (slap bass, sine bell lead, FM piano, etc.) — named generically | Tasks `02` (SQ), `04` (FM) | Project copyright; same model as the 12 factory `.psg` files (ADR-0004) |
| **GPL / CC-licensed community banks** with clear provenance, bundled as named subfolders with attribution | Furnace `tfilib` (already shipped, recategorised by Task `03`); Task `05` (additional packs) | ADR-0004 (Furnace `tfilib` precedent — GPL-compatible community FM bank) |
| **User-supplied VGM register logs** from public archives (vgmrips.net, Project2612) extracted via the existing `VgmExtract` pipeline | Task `06` (discoverability only — pipeline already built) | ADR-0019 |

**Never shipped:** patches extracted from game ROMs, patches named
after specific games / publishers / characters / level titles. This
matches the ADR-0004 line and the existing `.psg` factory authoring
rule.

## How to use a task file

Same conventions as `mvp2/README.md`:

1. Read the file top to bottom.
2. Read every doc under **Design references** — those carry the authoritative
   detail (patch system, browser design, factory bank rules). Tasks point
   at them rather than duplicating them.
3. Run the **Implementation steps** in order.
4. Run the **Verification** section. Every task ends with concrete runnable
   checks plus a listening test. If verification fails, the task is not done.

## Milestone map

| After task | Milestone | What changes for the user |
|-----------|-----------|---------------------------|
| 01 | **Generation tool ready** | `tools/patch-gen/` script can generate all FM and SQ patches via Claude API; dry-run works without API key |
| 02 | **SQ feels musical** | The 12 factory `.psg` presets have real ADSR shapes, idiomatic chorus detunes, and multi-channel patches that actually use multiple channels — flipping through the SQ bank reveals 12 distinct, useful sounds |
| 03 | **Patch tree is navigable** | Factory patches are organised by category (`bass/`, `lead/`, `keys/`, `brass/`, `pad/`, `drums/`, `fx/`) for FM and (`lead/`, `bass/`, `perc/`, `fx/`) for SQ; the browser tree reflects this; CMake stages the new layout into the bundle |
| 04 | **FM feels like Genesis** | ~40 original FM patches authored in the Genesis idiom (slap bass, brass stab, sine bell lead, FM piano, breath sax, kick/snare/hat, pads) ship alongside the Furnace `tfilib` set; users hear recognisable 16-bit sounds the moment they cycle through the factory bank |
| 05 | **Bank breadth** | One or more additional GPL / CC-licensed community FM banks ship as named, attributed subfolders, giving users hundreds of additional patches with clean provenance |
| 06 | **Game-original path is discoverable** | The patch browser's empty state, the About dialog, and the README all point users at `vgmrips.net` and the **Import Bank** button as the lawful way to extract patches from any Genesis game's audio |

## Task index

| # | Task | Delivers | Depends on |
|---|------|----------|-----------|
| 01 | [LLM patch generation pipeline](01-llm-patch-pipeline.md) | Generation tool ready | — |
| 02 | [Re-tune the 12 factory SQ presets](02-sq-preset-retune.md) | SQ feels musical | 01 (use pipeline to generate) |
| 03 | [Subfolder taxonomy + CMake staging + browser verification](03-patch-taxonomy.md) | Patch tree is navigable | mvp2/09 (browser is live) |
| 04 | [Original Genesis-idiom FM bank (~40 patches)](04-fm-original-bank.md) | FM feels like Genesis | 01, 03 |
| 05 | [Bundle additional GPL / CC community FM banks](05-gpl-community-packs.md) | Bank breadth | 03 |
| 06 | [VGM Import discoverability](06-vgm-import-discoverability.md) | Game-original path is discoverable | mvp2/09 |

Sequencing notes:

- `01` sets up the generation tool; run it first. `02` can use it immediately
  (SQ output goes into the existing flat `sq/` dir, no taxonomy needed).
- `03` is the structural prerequisite for `04` and `05` — FM patches go into
  subfolders that don't exist until `03` runs.
- `04` and `05` can run in either order or in parallel after `03` — they touch
  different subfolders.
- `06` is independent UI/copy work and can land any time after `03`
  (so the empty-state hint appears in the reorganised browser).

## Task file structure

Every task follows the same shape used in `mvp/` and `mvp2/`:

- **Header** — milestone tag, dependencies, design references.
- **Objective** — what this task delivers and why.
- **Context & key constraints** — gotchas and decisions from the design.
- **Scope** / **Out of scope** — what is in, and what defers.
- **Implementation steps** — concrete, ordered.
- **Deliverables** — files created or changed.
- **Verification** — runnable checks + a listening test.
- **Done when** — a final checklist.

## Out of scope for this chain

These are not sound-design tasks; they belong elsewhere or are
post-MVP:

- **New patch format (`.fmp` / `.gnfm` JSON for v2-native FM)** — TFI
  predates `freq_ctrl_mode`, `vel[]`, `channel_tl`, `fm_dac_prescaler`,
  so factory TFIs can't show off RYM2612-style modulation tricks.
  Defining a v2-native FM preset format is an ADR-level change and a
  separate effort.
- **Multi-instrument bank format mixing FM + SQ** — the retired
  `.gnbank` follow-up, listed in `mvp2/README.md` *Post-MVP backlog*.
- **Engine, DSP, or UI changes** — every task in this chain is
  authoring + light staging code. If implementation reveals an engine
  bug while tuning, file it separately; don't try to fix DSP under a
  sound-design task.
- **Tuning the Ladder Effect / Output Filter curves against measured
  hardware** — that's a DSP calibration task carried in
  `mvp2/README.md` *Open design questions*.
