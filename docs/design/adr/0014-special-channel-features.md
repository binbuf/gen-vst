# ADR-0014: Special-channel features under the one-channel-per-instance model

- **Status:** Accepted
- **Date:** 2026-05-21
- **Related:** [01-architecture.md](../01-architecture.md), [02-fm-synthesis.md](../02-fm-synthesis.md), [07-feature-spec.md](../07-feature-spec.md), [ADR-0010](0010-ymfm-instance-model.md), [ADR-0013](0013-multitimbral-voice-model.md)

## Context

[ADR-0010](0010-ymfm-instance-model.md) maps each FM voice to its own
`ymfm::ym2612` instance using **only channel 0**. Two YM2612 features are tied to
specific hardware channels and therefore have no home in that model:

- **DAC / PCM playback** is hardwired to **channel 6** — register `0x2B` (DACEN)
  makes channel 6 emit the `0x2A` DAC byte. With every voice on channel 0, no
  instance ever drives channel 6.
- **Channel 3 special mode** (register `0x27` bits 7:6) gives **channel 3**'s four
  operators independent pitches via registers `0xA8`–`0xAF`. It is meaningful only
  on channel 3.

`01-architecture.md` and `02-fm-synthesis.md` also carried "the voice allocator
must mark channel 6 unavailable" language inherited from a 6-channels-per-chip
mental model that ADR-0010 discarded.

## Decision

**DAC — a dedicated `ymfm` instance.** DAC playback uses a 17th `ymfm::ym2612`
instance, separate from the 16-voice pool of
[ADR-0013](0013-multitimbral-voice-model.md) and reserved exclusively for DAC. It
enables DACEN on its own channel 6 and is fed 8-bit PCM by `DACPlayer`. It is
never allocated as an FM voice, so no "exclude channel 6" logic is needed
anywhere.

**Channel 3 special mode — deferred to post-MVP.** It is not implemented in the
MVP. When it is built it needs no extra instance — a voice would simply use
channel 3 of its own `ymfm` instance and set register `0x27` — but it requires its
own UI surface and a second note-on path, and TFI/VGI/DMP patch files do not carry
its four independent pitches. `07-feature-spec.md` already lists it as a "beyond
Genny" extension, so deferring it does not affect Genny feature parity.

## Consequences

- The DAC instance is outside the voice pool of ADR-0013; total `ymfm` instances
  = 16 voices + 1 DAC.
- DAC is triggered via a dedicated MIDI channel, consistent with PSG routing in
  `03-psg-synthesis.md`. The loaded 8-bit PCM is **embedded in plugin state** so
  projects are self-contained — see `07-feature-spec.md`.
- The DAC instance runs at the YM2612 native rate and is summed into the FM mix
  bus before the single resampling pass — see [ADR-0011](0011-resampling-strategy.md).
- All "mark channel 6 unavailable" / "exclude channel 6 from FM allocation"
  language is removed from `01-architecture.md` and `02-fm-synthesis.md`.
- DAC is preserved as a Genny parity feature — it is on the parity checklist, not
  the extensions list.
- `07-feature-spec.md` marks Channel 3 special mode as post-MVP and keeps it on
  the extensions list with that note.

## Alternatives considered

- **3×6-channel instance model** — channels 3 and 6 would exist naturally, but
  ADR-0013's multitimbral parts need independent per-part LFO, which the single
  shared global LFO of a 6-channel instance cannot provide. Rejected; see
  [ADR-0010](0010-ymfm-instance-model.md).
- **Cut DAC from the MVP** — rejected: DAC is a Genny feature-parity item, and the
  Genny UI has a dedicated "D" section.
