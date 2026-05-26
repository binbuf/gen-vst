# Task 01 — Re-tune the 12 factory SQ presets

> **Milestone:** SQ feels musical — flipping through the factory `.psg`
> bank produces 12 distinct, useful sounds, not 12 minor variations on
> a flat-envelope square wave.
> **Depends on:** `mvp2/10` (the 12 stub `.psg` files exist;
> `loadDmpPsg` works; SQ panel is audible).
> **Design references:** `docs/design/04-patch-system.md`
> (*`.psg` Format*, *Factory `.psg` library* listing), the
> `SN76489Engine.h` ADSR range constants.

## Objective

The 12 factory `.psg` presets shipped by `mvp2/10` were sized as
"starting-point JSON" with a note that final tuning happens **by ear
against the live SQ panel**. Audit confirms several of them never got
that pass: `default.psg` is `atk=0, dr1=0, sus=0, dr2=0, rr=5` (a
square with a slow release and no envelope shape), `retro-beep.psg`
is `atk=0, dr1=0, sus=0, rr=0` (essentially silent after key-on), and
the multi-tone patches use detune values appropriate for chords
(±7 semitones = a fifth) where they describe chorusing (±1–2 semitones).

This task does the by-ear tuning pass that `mvp2/10` deferred. No new
files; no schema changes; no C++. Twelve `.psg` files get rewritten
in place until each one matches its described character on the live
SQ panel.

## Context & key constraints

### Current state — what's wrong

Audit of `extern/patches/sq/*.psg` (12 files) at the start of this
task:

| File | Issue | Fix direction |
|------|-------|---------------|
| `default.psg` | All zero ADSR except `rr=5`; effectively no envelope shape | Add modest attack, sustain plateau, and release suitable for a neutral lead |
| `square-bass.psg` | `atk=0, dr1=2, sus=4, rr=4` is reasonable but punchier values likely sound better | Tune dr1/sus for punchier bass character |
| `pulse-arp.psg` | `dr1=10, sus=14, rr=3` — high dr1 with high sus barely decays before reaching the plateau; loses the "arp" gap feel | Tighten dr1; drop sus so notes leave space between re-triggers |
| `soft-lead.psg` | Detune `+7` is a perfect fifth, not chorusing | Drop to ±1 (or ±2 cents-style) for actual chorus |
| `bright-pluck.psg` | `dr1=6, sus=0, rr=2` is in the right shape but values may need sharpening | Sharpen attack-to-silence trajectory |
| `retro-beep.psg` | `atk=0, dr1=0, sus=0, rr=0` — note dies on key-on | Pure-sustain with short rr; or short-decay beep |
| `chip-melody.psg` | Tone1+tone2 with detune `12` (octave) at half volume — OK shape, may need tuning | Verify octave layering balances against the lead |
| `detuned-chord.psg` | `+0/+7/−7` is a power chord — *intentional* per the docs; verify it actually sounds like a chord, not three out-of-tune leads | Confirm pan spread + envelope work; tune if dissonant |
| `noise-snare.psg` | `dr1=4, sus=15, rr=1` — sus=15 keeps noise at full attenuation after the brief decay, killing the "snap" | Drop sus toward 0 for a true snare crack |
| `periodic-bass.psg` | `dr1=2, sus=4, rr=5` is plausible; tune for buzz character | Verify on `type: periodic, rate: low` |
| `noise-hats.psg` | `dr1=2, sus=15, rr=0` — sus=15 holds noise audible; bad hat character | Drop sus toward 0 for a tight closed-hat |
| `title-screen.psg` | `tone1+tone2` detune `+5` (perfect fourth — a recognisable interval) + noise; verify it reads as a melody+harmony layer, not just two leads | Tune envelopes + noise level for "titlescreen" feel |

### Authoring rules (unchanged from `mvp2/10`)

- Every preset is an **original work**. No values are derived from
  game ROMs, SMPS drivers, DefleMask community packs, or any
  copyrighted source. The `.psg` format encodes only synthesis
  parameters (ADSR, vol, pan, detune, noise type/rate); none of those
  parameter choices is protectable. Names must remain generic — no
  game / character / publisher / level references.
- Each preset's **character description** in
  `docs/design/04-patch-system.md` *Factory `.psg` library* is the
  spec; tune toward that description, not toward any specific track.
- The `SN76489Engine` ADSR range constants in `src/SN76489Engine.h`
  are authoritative for the parameter ranges. Don't write values
  outside those ranges; the loader clamps but tuning against clamped
  values is misleading.

### Detune semantics

The `.psg` `detune` field is a **signed semitone offset** (per
`docs/design/04-patch-system.md` *`.psg` Format* schema), not cents.
This makes "chorus" detune coarse:

- `detune: 0` — unison.
- `detune: ±1` — a semitone apart; thick, slightly dissonant; the
  closest thing the format has to a chorus effect.
- `detune: ±2` — a whole step; clearly an interval, not chorus.
- `detune: ±5`, `±7`, `±12` — intentional harmonic intervals (fourth,
  fifth, octave).

`soft-lead.psg` should use `±1`. `chip-melody.psg` and
`detuned-chord.psg` keep their intentional intervals. `title-screen.psg`
keeps `+5` (deliberate harmony layer).

> **If a finer chorus is desired**, the post-MVP backlog could add a
> cents-resolution detune field to `.psg`. Out of scope for this task.

## Scope

### Files rewritten

All 12 files in `extern/patches/sq/`:

1. `default.psg`
2. `square-bass.psg`
3. `pulse-arp.psg`
4. `soft-lead.psg`
5. `detuned-chord.psg`
6. `bright-pluck.psg`
7. `retro-beep.psg`
8. `chip-melody.psg`
9. `noise-snare.psg`
10. `periodic-bass.psg`
11. `noise-hats.psg`
12. `title-screen.psg`

### No code changes

- No edits to `src/PsgPreset.{h,cpp}`.
- No edits to the schema in `docs/design/04-patch-system.md`.
- No edits to `tests/PsgPresetTests.cpp` (the existing factory-load
  smoke test continues to assert that every `extern/patches/sq/*.psg`
  parses; that contract is preserved).

## Out of scope

- Adding a 13th preset to the factory `.psg` set. The 12 files are
  the canonical bank under `mvp2/10`; growing the set is its own
  follow-up.
- Adding cents-resolution detune to the `.psg` format.
- Re-tuning anything in the FM bank — Task `03` handles original FM
  authoring; Furnace `tfilib` `.tfi` files stay untouched.
- Re-categorising the 12 presets into `sq/` subfolders. Task `02`
  does subfolder taxonomy across the whole patch tree; this task
  leaves the flat `sq/*.psg` layout alone so the two diffs don't
  collide.

## Implementation steps

1. **Read the character descriptions.** Open
   `docs/design/04-patch-system.md` *Factory `.psg` library* and read
   each of the 12 one-line characters. Tune **toward the description**,
   not toward your taste — the description is the contract.

2. **Build the plugin and load it in a host.**
   ```
   cmake --build build/windows-debug
   ```
   Load `GenVst.vst3` in a DAW (Reaper, Bitwig, FL Studio, etc.) or
   the Standalone target. Flip the mode selector to SQ. Open the
   patch browser; filter to SQ; load each factory preset in turn.

3. **For each of the 12 presets, tune by ear.** Play sustained notes,
   short notes, and chords (where the preset uses multiple channels).
   Adjust the JSON file in `extern/patches/sq/<name>.psg`, save,
   reload the preset in the browser (single-click) — the browser's
   *load = re-read from disk* contract means edits are picked up on
   the next click without restarting the host. Iterate until the
   preset sounds like its described character.

4. **Apply the specific known fixes** at minimum (see *Context — what's
   wrong* table above):
   - `default.psg`: give it an actual envelope, not pure release.
   - `pulse-arp.psg`: drop `sus` so re-triggers leave gaps.
   - `soft-lead.psg`: chorus detune `±1`, not `±7`.
   - `retro-beep.psg`: make it actually beep — non-zero `sus` or a
     short percussive shape.
   - `noise-snare.psg`: `sus` near 0 so the crack decays.
   - `noise-hats.psg`: `sus` near 0; tight `rr`.
   These are the audibly broken ones; the others need confirmation,
   not rescue.

5. **Cross-preset balance check.** Cycle through the 12 in order
   while playing the same MIDI input. Each preset's loudness should
   be roughly comparable — a `vol: 1.0` preset shouldn't be
   dramatically louder than another `vol: 1.0` preset. If two
   presets are wildly different in perceived loudness, adjust `vol`
   on the louder one. (Same-character presets — e.g., `square-bass`
   and `periodic-bass` — should sound like the same "instrument
   family.")

6. **Commit each file once it sounds right.** One commit per preset
   keeps the diff legible.

## Deliverables

- 12 rewritten files under `extern/patches/sq/`. No new files; no
  deleted files; no renames.
- No code, CMake, or test changes.

## Verification

1. **Parse check.** `cmake --build build/windows-debug && ctest -R PsgPreset --output-on-failure` — `PsgPresetTests` continues to pass; every file in `extern/patches/sq/` loads without error.
2. **Audible character check** (the main verification — this task is
   sound design): load each of the 12 factory presets in turn while
   playing a held middle-C and a short staccato arpeggio. For each
   preset, the sound must match its character description in
   `docs/design/04-patch-system.md`. Specifically:
   - `default.psg` produces a sound with a clear envelope, not a
     square wave that decays only via release.
   - `square-bass.psg` produces a punchy, bass-register tone.
   - `pulse-arp.psg`'s envelope leaves audible gaps between rapid
     re-triggers (play 16th notes; the notes should be perceptibly
     separated, not legato).
   - `soft-lead.psg` chorusing reads as "thickening", not as "two
     notes a fifth apart."
   - `detuned-chord.psg` reads as a triad, with the pan spread
     placing each tone in a different stereo position.
   - `bright-pluck.psg` decays to silence quickly (under ~300 ms on
     a default release pedal-off).
   - `retro-beep.psg` actually produces a sustained beep on key-hold.
   - `chip-melody.psg` reads as an octave-layered melody, not as a
     detuned lead.
   - `noise-snare.psg` reads as a snare hit (sharp attack, fast
     decay to near-silence).
   - `periodic-bass.psg` reads as a buzzy pitched bass, not white
     noise.
   - `noise-hats.psg` reads as a closed hi-hat (very short).
   - `title-screen.psg` reads as a melody + harmony layer, with the
     noise texture audible but not overpowering.
3. **Cross-preset loudness check.** Cycle through the 12 presets with
   a held middle-C MIDI input; no preset is more than ~6 dB louder or
   quieter than any other at the same `vol` setting.
4. **No regressions in the existing path.** `pluginval
   --strictness-level 8` continues to pass on the artefact; the SQ
   panel still reflects loaded values on each preset load.

## Done when

- [ ] Every one of the 12 `.psg` files in `extern/patches/sq/` has
      been opened in the live SQ panel and tuned by ear against its
      character description.
- [ ] The five known-broken presets listed in step 4 are fixed:
      `default`, `pulse-arp`, `soft-lead` (chorus detune), `retro-beep`,
      `noise-snare`, `noise-hats`.
- [ ] `ctest` is green (no parsing regressions).
- [ ] A casual listener flipping through the 12 presets perceives 12
      distinct sounds, not minor variations on a square wave.
- [ ] `pluginval --strictness-level 8` continues to pass.
