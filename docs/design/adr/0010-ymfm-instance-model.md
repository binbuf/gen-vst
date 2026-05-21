# ADR-0010: ymfm voice instance model

- **Status:** Accepted
- **Date:** 2026-05-21
- **Related:** [01-architecture.md](../01-architecture.md), [02-fm-synthesis.md](../02-fm-synthesis.md), [ADR-0002](0002-ymfm-for-fm-emulation.md), [ADR-0013](0013-multitimbral-voice-model.md), [ADR-0014](0014-special-channel-features.md)

## Context

Gen VST targets 16-voice FM polyphony, well beyond the YM2612's hardware 6
channels. There are two ways to map voices onto `ymfm::ym2612` instances:

1. **16 × 1-channel instances** — one `ymfm::ym2612` per voice, each using only
   channel 0. Global registers (LFO, timers) are fully isolated per voice, so
   there is no register aliasing between voices.
2. **3 × 6-channel instances** — 18 voices total, far less ymfm object overhead,
   but the single global LFO is shared by all 6 channels in an instance, so
   per-voice LFO/timer isolation is lost.

## Decision

Use **16 independent 1-channel `ymfm::ym2612` instances** (one voice each,
channel 0 only). This eliminates global-register aliasing between voices.

[ADR-0013](0013-multitimbral-voice-model.md) independently requires this layout:
the six-part multitimbral model needs each part to have its own LFO setting, and
only one global LFO exists per `ymfm` instance. Sharing a 6-channel instance
(the 3×6 alternative) would force multiple parts to share one LFO. 16 one-channel
instances give every voice — and therefore every part — an isolated LFO.

## Consequences

- No cross-voice bleed of LFO or timer state; each voice's patch is fully
  self-contained.
- Higher memory footprint and per-block CPU than instance-sharing. The CPU cost
  of 16 instances at 44,100 Hz remains a **profiling check during
  implementation**; the `VoiceAllocator` abstraction hides the instance layout
  from the rest of the plugin, keeping the decision reversible if profiling
  demands it.
- DAC and Channel 3 special mode are tied to specific hardware channels (6 and 3)
  and have no home in a "channel 0 only" model. They are handled separately in
  [ADR-0014](0014-special-channel-features.md): DAC uses a dedicated 17th
  instance; Channel 3 special mode is deferred post-MVP.
- The FM mix bus is resampled **once** after summing all voices — there is no
  per-voice resampler (see [ADR-0011](0011-resampling-strategy.md)).

## Alternatives considered

- **3 × 6-channel instances (18 voices)** — less ymfm object overhead, and
  hardware channels 3 and 6 would exist naturally, but the shared global LFO
  breaks the per-part LFO isolation required by
  [ADR-0013](0013-multitimbral-voice-model.md). Rejected.
