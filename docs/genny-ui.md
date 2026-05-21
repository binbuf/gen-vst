# Genny VST — UI/UX Specification

## Overall Aesthetic
A pixel-perfect skeuomorphic emulation of late-80s/early-90s rackmount hardware, filtered through 16-bit console aesthetics. Everything is rendered as if it were drawn at 1x pixel scale (no anti-aliasing, no rounded sub-pixel edges) and then optionally scaled up. The mood is **dark plastic chassis** with **glowing CRT/LCD insets** and **chunky physical knobs**. Think: a fictional 1991 FM synth that Yamaha never shipped.

## Color Palette
- **Chassis background:** pure black `#000000`
- **Panel insets:** very dark gray `#0a0a0a` with 1px lighter top/left bevels (`#2a2a2a`) and 1px darker bottom/right bevels for a subtle embossed look
- **LCD screen base:** muted olive-green `#3d5a2e` → `#4a6b38`
- **LCD active pixels:** bright phosphor green `#a8d878` to `#c8e89a`
- **LED segment display base:** very dark red-black `#2a0808`
- **LED segment active:** vivid red `#ff2020` with a slight bloom
- **Knob body:** mid blue `#4a78c8` with a darker blue ring `#1e3a6e` and a small white/cyan indicator dot
- **Logo yellow:** `#f5c842` with `#c89020` shadow, red underline `#d83030`
- **Label text:** off-white `#e0e0e0` in a small bitmap font
- **Selected item highlight:** red `#cc2020` outline or red dot
- **Sliders:** thin dark groove with a chunky blue rectangular cap

## Typography
One bitmap font everywhere, roughly 8px tall, all caps for labels. Two specialty fonts: a **7-segment LED font** for the big patch-name display, and a **dot-matrix LED font** (5x7) for small red numeric readouts next to knobs. The "GENNY" wordmark is a custom chunky pixel logotype with a beveled gold gradient and a single red underline stroke.

## Layout
A fixed-size window roughly 960×560px, split into four regions:

**Top header bar (full width, ~80px tall)** — logo on the left, a tiny green VU meter labeled "TRUE STEREO" next to it, and a wide red 7-segment patch-name display spanning the right ~60%. The display has small decorative bracket glyphs flanking the patch name ("GADGET BASS 01").

**Middle row, three columns:**
- **Left column (~25%):** LFO/Algorithm panel
- **Center column (~45%):** Instruments list + Channel routing
- **Right column (~30%):** Presets list

**Bottom row (full width, ~220px tall):** Four identical **operator panels** side-by-side (the FM operators, since the YM2612 has 4 operators per voice).

## Left Column — LFO & Algorithm
Top sub-panel has three small blue knobs in a row labeled **LFO**, **AMS**, **FMS**, each with a tiny red LED readout to its right showing the current value (default `0`). Each knob has a small red power dot beside its label.

Below that, an **ALGORITHM** row with eight square buttons numbered 1–8 in a single line. The currently selected algorithm (button 3 in the screenshot) is wrapped in a red circular outline rather than filled — it looks like a stamped indicator ring.

Below the buttons, a wider green LCD inset displays a **mini wiring diagram** of the selected FM algorithm: small labeled boxes `S1` `S2` `S3` `S4` connected with lines showing operator routing (which operators are carriers vs. modulators). The diagram must redraw whenever a different algorithm button is clicked — there are 8 canonical YM2612 routings.

To the left of the diagram is a **FEEDBACK** knob with its own red readout showing either a number 0–7 or `OFF`.

## Center Column — Instruments & Channel Routing
The dominant element is a **green LCD list panel** titled **INSTRUMENTS** containing scrollable rows. Each row shows a small icon (waveform glyph for tonal instruments, a tiny drum-kit pixel icon for percussion) followed by the instrument name in dark green text on the LCD background. The selected row is rendered in inverse video or with a subtle outline. A pixel scrollbar runs down the right edge of the list with a chunky thumb.

To the right of the list, a vertical stack of small control groups:
- **FM / SQ / D** — three small pill buttons (chip-section selector: FM synth, PSG square wave, DAC sample channel)
- **CHANNELS** — six small numbered buttons `1 2 3 4 5 6` in a row (the YM2612's six FM channels). Selected channel gets a red highlight.
- **MIDI** — a single-value selector showing `1` with up/down step arrows
- **TRANSPOSE** — two paired numeric fields (semitones and octaves, both `0`) each with up/down arrows
- **RNG** — note range display showing `0-127`
- **DEL** — horizontal slider with a red readout `0`
- **PAN** — horizontal slider with no visible readout (or a centered indicator)

## Right Column — Presets
Two tab headers at top: **PRESETS** (active) and **IMPORT**. The body is a second green LCD list, same styling as the Instruments list but without icons — just preset names like `Gadget Bass 01`, `Perc Hat 01`, `Shinobi Bass`. Selected row inverted. Pixel scrollbar on the right. Small folder/lock icons in the top-right of the tab header.

## Bottom Row — Four Operator Panels
Four identical vertical strips, one per FM operator. Each strip contains, top to bottom:

1. **Operator number badge** in the top-left corner — a blue square with a white number (1, 2, 3, 4) and a red status dot beside it.
2. **LEV (envelope display)** — a wide green LCD showing the **ADSR envelope shape** as a pixel-drawn line graph. The shape is computed live from the five envelope knobs below it. Each operator's curve looks different in the screenshot, confirming the graph is data-driven.
3. **Five blue knobs in a row** labeled `ATK` `DR1` `SUS` `DR2` `RR` (Attack, Decay Rate 1, Sustain Level, Decay Rate 2, Release Rate — the YM2612's standard envelope stages).
4. **Four horizontal sliders** stacked, each with a red LED value readout on the right:
   - `DETUNE`
   - `FREQ` (multiplier)
   - `ENV SCALE` (rate scaling)
   - `LFO` (with a small red enable dot) paired with `SSG` (SSG-EG mode selector showing `OFF` or a mode number)

The four panels are visually separated by thin vertical dividers but share the same baseline grid so the knob rows and slider rows align horizontally across all four operators — this alignment is important and gives the bottom region its "mixing console" feel.

## Interaction Details Worth Encoding
- Knobs respond to **vertical click-drag** (up = increase). Indicator dot rotates roughly 270° from min to max, with the rest position at 7 o'clock.
- LED and LCD readouts update live as values change; no animation, just instant pixel redraw.
- Clicking an algorithm button rewires the mini diagram and may visually re-color which operators are "carriers" vs. "modulators."
- Selecting a list item (instrument or preset) repaints every knob, slider, LED, and the patch-name display in one batch.
- Everything is **pixel-snapped** — no smooth gradients, no drop shadows beyond a single hard pixel offset. If you implement this in HTML/CSS, set `image-rendering: pixelated` and avoid border-radius entirely; use 1-2px solid borders to fake bevels.

## One-Sentence Vibe
*A black rackmount unit from a parallel-universe 1991 where Sega licensed Yamaha to ship a 1U FM synth, photographed under fluorescent light and rendered entirely in 8×8 tiles.*