# ADR-0013: Six-part multitimbral architecture with a shared 16-voice pool

- **Status:** Superseded by [ADR-0021](0021-three-mode-single-engine-ui.md) (2026-05-24). Was Accepted 2026-05-21.
- **Date:** 2026-05-21
- **Related:** [ADR-0021](0021-three-mode-single-engine-ui.md), [ADR-0010](0010-ymfm-instance-model.md), [ADR-0014](0014-special-channel-features.md)
- **Historical note:** v2 abandons the six-part multitimbral model in favour of a single-engine-per-instance design (FM, SQ, or D, set per instance). The shared 16-voice ymfm pool from ADR-0010 is **retained** for FM mode polyphony; only the parts/routing layer goes away.

## Context

The design docs described two incompatible instruments. `01-architecture.md`
modelled a **16-voice polysynth** — a `VoiceAllocator` owning `Voice[0..15]`, LRU
stealing, and one active patch. `05-ui-ux.md` and `genny-ui.md` are built around
**six FM channels** — a 1–6 channel selector, an `apvts` holding parameters "for
all 6 channels", and FM-channel paging. `07-feature-spec.md` contained both, plus
per-PSG-channel MIDI assignment (channels 11–14) that only makes sense if FM is
multi-channel.

A polysynth and a multitimbral instrument differ in their voice allocator, `apvts`
layout, MIDI routing, patch-load semantics, and state model. The contradiction had
to be resolved before any of those components could be written.

## Decision

Gen VST is a **six-part multitimbral instrument backed by a shared pool of 16 FM
voices**.

- **Parts.** There are 6 FM parts. Each part has its own patch and its own
  assigned MIDI channel. The 6 parts are the `CHANNELS 1–6` selector in the Genny
  UI; the `apvts` holds a full FM parameter set per part.
- **Voice pool.** There are 16 `ymfm::ym2612` voice instances in a single shared
  pool (one channel per instance — see [ADR-0010](0010-ymfm-instance-model.md)).
  Voices are **not** statically bound to parts.
- **Allocation.** A note-on arrives on a MIDI channel → the part bound to that
  channel is identified → a voice is taken from the pool (LRU steal if none free),
  loaded with that part's patch, and keyed on. Total polyphony is **16 notes
  shared across all active parts** — this is the "16 voices, beyond Genny's
  hardware 6" goal: the pool size, not a per-part limit.
- **Editing.** The UI edits one part at a time via the channel selector. This is
  exactly the FM-channel paging already described in `05-ui-ux.md`.

## Consequences

- `01-architecture.md` is rewritten around parts + a voice pool; the
  "16-voice polysynth" framing is removed.
- The `apvts` holds 6 parts × the full FM parameter set (~50 params/part ≈ 300 FM
  parameters), plus PSG, DAC and global params. This is within VST3/AU limits and
  is the reason FM-channel relays are named without a `_ch<n>` suffix and rebind
  on part selection (`05-ui-ux.md`).
- `VoiceAllocator` manages a 16-voice pool plus the part↔MIDI-channel binding
  table — it is not a fixed `Voice[0..15]` array owned one-per-channel.
- Voice stealing is global LRU across the whole pool. Per-part voice
  reservations / caps (so one busy part cannot starve the others) are a possible
  post-MVP refinement, out of MVP scope.
- Loading an Instrument/Preset targets the **currently selected part** — this
  resolves `05-ui-ux.md` open question #5. A multi-part "performance" file that
  loads all 6 parts at once is out of MVP scope.
- Program Change on a MIDI channel loads a patch into that channel's part (the
  index mapping is in `07-feature-spec.md`).
- State persistence stores, per part: the assigned MIDI channel and the active
  patch identified **by path** (not a flat bank index). See `01-architecture.md`.
- The single global LFO is effectively per-voice in this model (each voice is its
  own `ymfm` instance — [ADR-0010](0010-ymfm-instance-model.md)), so each part's
  patch carries its own LFO settings with no cross-part bleed.

## Alternatives considered

- **Plain 16-voice polysynth, single patch** — rejected: the simplest voice
  allocator, but it orphans the six-channel Genny UI that is the project's visual
  identity; `05-ui-ux.md` and `genny-ui.md` would have to be discarded or heavily
  reworked.
- **Polysynth for the MVP, multitimbral later** — rejected: the voice allocator
  and `apvts` layout would be built twice, and the UI would ship against a model
  it then has to abandon.
