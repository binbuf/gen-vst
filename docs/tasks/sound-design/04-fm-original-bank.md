# Task 04 — Original Genesis-idiom FM bank

> **Milestone:** FM feels like Genesis — when a user cycles through the
> factory FM bank they hear recognisable 16-bit timbres (slap bass,
> sine bell lead, FM piano, brass stab, breath sax, kick/snare/hat),
> not a generic OPN library.
> **Depends on:** `03-patch-taxonomy.md` (the `fm/<category>/` folders
> exist; `factoryPatches` enumerates recursively).
> **Design references:** `docs/design/02-fm-synthesis.md` (operator
> algorithms, envelope semantics, register ranges),
> `docs/design/04-patch-system.md` (*TFI Format*, *VGI Format*,
> *Factory bank*), ADR-0004, ADR-0010 (16-voice ymfm pool).

## Objective

Ship ~40 original FM patches authored in the Genesis idiom, organised
into the seven `fm/<category>/` folders from Task 03. Patches are
**original works** authored from scratch — no values copied from game
ROMs, SMPS drivers, or any copyrighted source — named generically.
Distributed file format is `.tfi` (primary) with `.vgi` for patches
that need AMS / PMS / AMON (LFO modulation, tremolo, vibrato sounds).

After this task, opening the patch browser and clicking through the
Factory FM tree produces a tour of 16-bit-Genesis-feel sounds: a
recognisable slap bass under `bass/`, a sine bell lead and an FM
piano under `lead/` and `keys/`, brass and pad sounds for `brass/`
and `pad/`, an arcade-style kick/snare/closed-hat under `drums/`,
and a few `fx/` zaps. The existing Furnace `tfilib` patches stay; the
new bank ships alongside them, distinguished by category folders
(Furnace's generic names like `bass.tfi` and `piano.tfi` end up
beside the new originals — that's expected, the originals just sound
more Genesis-idiomatic).

## Context & key constraints

### Legal model — what makes a patch "original"

Same rule that governs the 12 factory `.psg` files (Task 10 of mvp2,
documented in `04-patch-system.md` *Factory `.psg` library*):

- **Original** = the developer set every register value by hand,
  guided by synthesis theory (algorithm choice, modulator-to-carrier
  ratios, envelope shapes) and by listening on the live FM panel.
- **Not original** = values copied from a tool that extracted them
  from a game ROM (Y12 / SMPS rips), from a published patch list
  for a specific game, or from any other copyrighted source.
- **Allowed inspiration** = "this should sound like the bass register
  in a 16-bit action platformer" (a *category* of sound). The
  developer authors values that produce that *category* of sound.
- **Not allowed inspiration** = "this should match the bass from
  Streets of Rage 2 stage 1." (Naming-by-game or values-from-game.)

Names must be generic and category-descriptive. Acceptable: `Slap
Bass 1`, `FM Piano Bright`, `Brass Stab`, `Breath Sax`, `Sine Bell
Lead`. Not acceptable: any game / publisher / character / level
title; any composer name; any track title.

### The 16-bit Genesis idiom — what to author toward

Each category gets a target patch count and a list of timbre
sub-types to cover. The counts assume one patch per sub-type unless
a sub-type warrants variants (e.g., bright vs. soft FM piano).

**`bass/` — 6 patches** (joins existing Furnace `bass.tfi`,
`distbass.tfi`, `distslap.tfi`, `elecbass.tfi`, `slapbass.tfi`,
`wobble_bass.vgi`):

| File | Character |
|------|-----------|
| `slap-bass-1.tfi` | The defining Genesis slap bass — punchy attack, mid-range carrier with a fast-decaying modulator giving the "thwack" |
| `slap-bass-2.tfi` | Brighter / harder variant; higher modulator MUL for more high-frequency bite |
| `synth-bass-warm.tfi` | Smooth FM bass for ballads / pads / RPG dungeon themes; longer attack, sustained sine-ish carrier |
| `synth-bass-pluck.tfi` | Short percussive bass; fast decay to silence; useful for staccato basslines |
| `acid-bass.tfi` | Resonant-feeling bass via aggressive feedback on OP1; squelchy character |
| `sub-bass.tfi` | Pure low-frequency carrier with minimal modulation; for sub-frequency reinforcement |

**`lead/` — 7 patches** (joins the existing Furnace leads):

| File | Character |
|------|-----------|
| `sine-bell-lead.tfi` | The pure FM bell-toned lead common in 16-bit RPG melody lines; near-sine carrier with a single high-MUL modulator for the bell shimmer |
| `square-pulse-lead.tfi` | FM approximation of a Master System square — harder, more buzz |
| `breath-sax.tfi` | Soft-attack sax voice with the slight breath noise feel that low-feedback FM does well |
| `flute-soft.tfi` | Pure FM flute — sine-like carrier, gentle attack, slow release |
| `whistle-lead.tfi` | High register pure-tone lead suitable for melody doubling |
| `pluck-lead.tfi` | Short percussive lead with fast attack and fast decay; good for staccato melody lines |
| `tremolo-lead.vgi` | Lead with LFO-driven AMS for tremolo wobble — uses VGI's AMS/PMS bytes (Furnace `tfilib` `vibrato_lead.vgi` is the closest existing analogue; this is a tremolo, not vibrato, complement) |

**`keys/` — 6 patches** (joins the existing Furnace piano variants):

| File | Character |
|------|-----------|
| `fm-piano-bright.tfi` | The bright FM piano sound — classic algorithm 5 with bell-like overtones |
| `fm-piano-soft.tfi` | Mellower variant for ballads; less modulator depth |
| `electric-piano.tfi` | Rhodes / DX7-style EP voice — softer attack, tine-like overtones |
| `harpsichord-bright.tfi` | Sharper plucked-key voice with fast attack and fast decay; bright modulator |
| `vibes.tfi` | Vibraphone-like soft mallet sound — slow attack, long sustain, gentle decay |
| `music-box.tfi` | High-register bell-like keys; very short percussive envelope |

**`brass/` — 5 patches** (joins existing Furnace `fifths.tfi`,
`shimmer_brass.vgi`):

| File | Character |
|------|-----------|
| `brass-stab.tfi` | Punchy short brass hit — fast attack, fast decay to silence |
| `brass-section.tfi` | Sustained brass voice with fuller carrier sound; the JRPG / strategy-game "big horn" |
| `trumpet-bright.tfi` | Solo-trumpet character with clear attack transient |
| `horn-soft.tfi` | French horn-like mellow voice; slow attack, sustained body |
| `synth-brass-pad.vgi` | Slow-attack synth brass with LFO movement for pad use (VGI for AMS/PMS) |

**`pad/` — 5 patches** (joins existing Furnace pads):

| File | Character |
|------|-----------|
| `warm-pad.tfi` | Long-attack sustained pad; for ambient backdrops |
| `ethereal-pad.vgi` | Slow-attack pad with LFO-driven PMS for shimmering pitch movement |
| `string-pad.tfi` | FM approximation of synth strings — slow attack, slight modulation depth for ensemble feel |
| `choir-pad.tfi` | Voice-like pad — formant-feel via specific modulator ratios |
| `dark-pad.tfi` | Low-register sustained pad with modulator detune for dissonance / tension |

**`drums/` — 7 patches** (joins existing Furnace drum patches):

| File | Character |
|------|-----------|
| `kick-tight.tfi` | Punchy short kick — fast pitch envelope on the carrier, fast amplitude envelope |
| `kick-deep.tfi` | Longer-decay sub kick |
| `snare-bright.tfi` | FM snare with noise-like attack via high-MUL modulator + fast decay |
| `snare-tight.tfi` | Shorter snare crack |
| `tom-1.tfi` | Mid-tom pitch envelope, percussive decay |
| `tom-2.tfi` | Lower-pitched tom variant |
| `closed-hat.tfi` | High-frequency percussive hit — high MUL modulator, very fast decay (FM hats are harsh; that's authentic) |

**`fx/` — 4 patches**:

| File | Character |
|------|-----------|
| `laser-zap.tfi` | Fast pitch sweep — useful via AUTO_RETRIG mode for arcade laser-style effects (though the patch itself uses INT_MUL; the AUTO_RETRIG behaviour is host-driven) |
| `bell-hit.tfi` | Very bright bell with long ringing decay |
| `noise-burst.tfi` | High-MUL operator producing noise-like texture; short envelope |
| `risetransition.tfi` | Long slow attack patch suitable for transitions / build-ups |

### Format selection — when to use `.tfi` vs `.vgi`

- **`.tfi` (42 bytes)** — primary format. Carries ALG, FB, and the
  10 per-operator parameters (MUL, DT, TL, KS, AR, DR, SR, RR, SL,
  SSG-EG). **Does not carry** AMS, PMS, AMON, or LFO enable/rate.
  Use `.tfi` for any patch that doesn't need LFO modulation.
- **`.vgi` (43 bytes)** — extends TFI with AMS/PMS in byte 2 and
  AMON packed into each operator's DR byte. Use `.vgi` for patches
  that specify LFO depth (`tremolo-lead`, `ethereal-pad`,
  `synth-brass-pad`).
- **`.y12` (128 bytes)** — ADR-0004 forbids shipping Y12 as factory
  content. Don't use.

### Authoring workflow (per patch)

This is what the LLM + human collaboration looks like for each
patch:

1. **LLM authors initial values.** Given the patch description above,
   choose algorithm, feedback, per-operator MUL/DT/TL/AR/DR/SR/RR/SL,
   based on FM synthesis theory:
   - For brass / pad: lower algorithms (0–3) with more modulators in
     chain.
   - For bells / bright EPs: algorithm 5 or 4 with a single
     modulator-carrier pair plus a parallel carrier.
   - For organs / pads: algorithm 7 (all four operators as carriers
     in parallel).
   - Use the YM2612 operator behaviour notes in
     `docs/design/02-fm-synthesis.md` to guide the choice.
   - Author the byte buffer directly: a 42-byte `.tfi` is small
     enough that a script (or a careful pass with the FM panel UI)
     can generate it. Keep all values inside their documented
     hardware ranges.

2. **Human tunes by ear.** Open the plugin; load the new patch; play
   sustained, percussive, and chord-form notes; adjust TL on each
   operator (volume balance), AR/DR/SL/RR (envelope shape), and
   MUL/DT (timbre) until the patch matches its description. Save
   the tuned values back as `.tfi` / `.vgi`. The FM panel's *Save*
   button writes `.tfi` natively; *Export VGI* writes the `.vgi`
   form (`04-patch-system.md` *Folders, Import & Export*).

3. **Commit one patch at a time** so the diff is reviewable. Commit
   message format: `factory: add fm/<category>/<name>.tfi (<one-line
   description>)`.

### Avoiding the "Furnace tfilib already covers this" trap

Several of the proposed file names overlap with existing Furnace
`tfilib` files now living under the new taxonomy (e.g.,
`fm/bass/slapbass.tfi` already exists; the new `slap-bass-1.tfi`
lives alongside it). That's intentional — the existing Furnace
patches are GPL-attributed community work; the new originals are
authored from scratch to feel more Genesis-idiomatic. Users can
A/B them. Don't replace Furnace files; ship the originals as
additions.

The naming convention disambiguates: Furnace files use the bank's
original short names (`slapbass.tfi`); originals use
hyphen-separated descriptive names (`slap-bass-1.tfi`). The patch
browser displays both; the visual distinction is obvious.

## Scope

### New files

~40 new `.tfi` and `.vgi` files distributed across the seven
`fm/<category>/` subfolders per the tables above. Every file is an
original work under the project's GPLv3 copyright.

### No code changes

- No edits to loaders, browser, CMake, or tests. Task 03's CMake
  recursive glob picks up the new files automatically; the browser
  enumerates them automatically; existing loader tests still pass
  because the loaders are unchanged.

## Out of scope

- A new patch format for v2-native FM features (`freq_ctrl_mode`,
  `vel[]`, `channel_tl`, `fm_dac_prescaler`). Listed in
  `README.md` *Out of scope for this chain* — needs an ADR.
- Authoring SQ patches (handled by Task 02's re-tune + the existing
  `mvp2/10` set).
- Retiring or replacing Furnace `tfilib` files. They stay as the
  GPL-attributed community bank; this task adds an *additional*
  layer of originals beside them.
- Adding documentation for each patch beyond the filename. The 1-line
  description in this task is enough; users learn the sound by
  listening.

## Implementation steps

1. **Verify dependency.** Confirm Task 03 has shipped: the
   `extern/patches/fm/<seven categories>/` folders exist, the
   CMake glob walks them recursively, the browser shows the
   category tree.
2. **Set up a target list and a tuning DAW project.** Pin the 40-row
   list from the *Context — The 16-bit Genesis idiom* tables.
   Create a simple DAW project with a single MIDI track routed to
   GenVst FM mode; this is the listening environment.
3. **Author each patch one at a time** by the workflow in *Context —
   Authoring workflow (per patch)*:
   - Pick the next file from the target list.
   - LLM writes the initial 42-byte (or 43-byte) buffer with values
     guided by synthesis theory + the patch description.
   - Human loads it, plays test notes, tunes via the FM panel UI,
     saves back as `.tfi` / `.vgi`.
   - Commit.
4. **Cross-category sweep.** Periodically (every ~10 patches),
   cycle through every committed original and confirm:
   - Each one matches its description.
   - Loudness across the bank is roughly comparable (no patch is
     dramatically louder/quieter than its peers).
   - No two patches in the same category sound identical (i.e.,
     the variant differentiation actually reads).
5. **Final A/B against Furnace.** With the full bank committed,
   cycle Furnace `tfilib` patches vs. new originals in each
   category. The originals should sound *more* Genesis-idiomatic;
   if any original sounds worse than the Furnace patch in the same
   category, re-tune.

## Deliverables

- ~40 new `.tfi` and `.vgi` files under
  `extern/patches/fm/<seven categories>/`. Exact count is the sum
  of the per-category targets above (6 + 7 + 6 + 5 + 5 + 7 + 4 = 40).
- No new code, no CMake changes, no test changes (Task 03 prepared
  the build/scan path; new files are automatically picked up).
- Commit history: one commit per patch (or per logical pair, e.g.,
  `kick-tight.tfi` + `kick-deep.tfi`).

## Verification

1. **File count.** `find extern/patches/fm -name "*.tfi" -o -name
   "*.vgi" | wc -l` shows the existing Furnace count plus the new
   originals (~40 added).
2. **Build.** `cmake --build build/windows-debug` succeeds; the
   bundle's `Resources/patches/fm/<category>/` mirrors the new
   files.
3. **Parse check.** `ctest -R PatchLoader --output-on-failure`
   passes — every new file is loadable by the existing TFI/VGI
   loaders. (If any new file fails to parse, the authoring made an
   out-of-range value; clamp and re-author.)
4. **Audible category check** (the main verification — this task is
   sound design): cycle through every new patch in the patch
   browser. For each one:
   - It loads without errors.
   - It produces sound on key-on.
   - It matches the character described in the table above.
5. **Recognisability check.** Play a short test phrase (e.g., a
   melodic line for `lead/`, a walking bass for `bass/`, a chord
   stab for `brass/`, a drum loop for `drums/`) through one patch
   per category. A casual listener should be able to identify each
   category's role without prompting (i.e., "that's a bass", "that's
   a lead", "that's a snare hit").
6. **No regressions.** All existing Furnace `tfilib` patches still
   load correctly (a hash / parse check before and after the work,
   or just a `ctest` pass).
7. **Pluginval.** `pluginval --strictness-level 8` continues to
   pass.

## Done when

- [ ] All ~40 patches listed in the *16-bit Genesis idiom* tables
      have been authored and committed in their respective
      `fm/<category>/` folders.
- [ ] Every new file parses via the existing TFI/VGI loaders
      (no out-of-range values).
- [ ] Every new patch matches its character description on the live
      FM panel.
- [ ] Loudness across the bank is balanced (no >6 dB outliers at
      identical `vel` and `tl`).
- [ ] Furnace `tfilib` patches still load alongside the originals.
- [ ] `pluginval --strictness-level 8` continues to pass.
