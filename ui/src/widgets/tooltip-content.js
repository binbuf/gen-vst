// Canonical per-widget tooltip content. Schema: a flat object mapping
// widget id / param name -> { name, desc }.
//
// Each widget mount reads its entry once and writes the two data-tip-*
// attributes onto the control's host element. The hover handler in
// tooltip.js reads them on hover (05-ui-ux.md *Tooltip system*).
//
// Length contract:
//   - name: <= 32 chars, uppercase.
//   - desc: <= 120 chars, one plain-English sentence.
//
// Per-mode panel entries (FM/SQ/D) are added by Tasks 05-07 as their
// widgets land. Task 04 ships the gallery + header + Settings entries.

export const TOOLTIPS = {
  // --- Header --------------------------------------------------------
  "tooltips_enabled": {
    name: "TOOLTIPS",
    desc: "Toggle these hover descriptors. Off hides every tooltip globally.",
  },
  "master_volume": {
    name: "MASTER VOLUME",
    desc: "Output trim applied after every mode. Soft-clipped at the rails.",
  },
  "output_filter": {
    name: "OUTPUT FILTER",
    desc: "Legacy = console-style band-limited output. Crystal clear = bypass.",
  },
  "ladder_effect": {
    name: "LADDER EFFECT",
    desc: "Pre-DAC ladder-filter colour; mimics the Genesis 1's analog stage.",
  },
  "mode_select": {
    name: "MODE",
    desc: "Switch between FM (YM2612), SQ (SN76489 PSG) and D (sample decimator).",
  },
  "wordmark": {
    name: "ABOUT",
    desc: "Open the About / credits panel — version, license attributions and source link.",
  },
  "patch_prev": {
    name: "PREV PATCH",
    desc: "Step to the previous patch in the active mode's sorted list.",
  },
  "patch_next": {
    name: "NEXT PATCH",
    desc: "Step to the next patch in the active mode's sorted list.",
  },
  "patch_browse": {
    name: "BROWSE PATCHES",
    desc: "Open the preset browser. Greyed in D mode — that mode has no presets.",
  },
  "dac_prescaler": {
    name: "DAC PRESCALER",
    desc: "FM: DAC clock divider (0 = bypass). D: sample-rate decimator; 8-bit quantize is always on. SQ: bypassed.",
  },
  "settings": {
    name: "SETTINGS",
    desc: "Open the Settings panel — hardware strict, UI scale, aftertouch routing.",
  },
  "hardware_strict": {
    name: "HARDWARE STRICT",
    desc: "Clamp to YM2612-faithful limits — 6 voices, single FLOAT_MUL/AUTO_RETRIG, filter/ladder forced on.",
  },
  "velocity_to_tl": {
    name: "VELOCITY → TL",
    desc: "Global MIDI velocity → carrier TL scaling. Per-op vel[op] depth still applies when off.",
  },
  "aftertouch_target": {
    name: "AFTERTOUCH",
    desc: "Channel pressure routing — off, LFO PMS (vibrato depth), or Carrier TL (per-algorithm carriers).",
  },
  "ui_scale": {
    name: "UI SCALE",
    desc: "Whole-window integer zoom: 1× / 2× / 3×. Persisted across plugin loads.",
  },
  "reset_all": {
    name: "RESET ALL TO DEFAULTS",
    desc: "Snap every parameter to its default and clear the active patch path. Requires confirmation.",
  },

  // --- D mode panel --------------------------------------------------
  "dry_wet": {
    name: "DRY / WET",
    desc: "Blend between unprocessed input (0) and fully decimated signal (1).",
  },
  "mono": {
    name: "MONO",
    desc: "Sum L+R after decimation so both output channels carry the same signal.",
  },

  // --- Compound widgets with no single param --------------------------
  // FM operator envelope canvas (per-op AR/D1R/SL/D2R/RR curve).
  "envelope_curve": {
    name: "ENVELOPE",
    desc: "ADSR shape for the selected operator — AR/DR/SL/SR/RR with KEY ON / KEY OFF markers.",
  },
  // FM 8-algorithm topology diagram (operator routing graph).
  "algorithm_topology": {
    name: "TOPOLOGY",
    desc: "Operator routing diagram for the selected algorithm — modulators feed carriers; arrow exits each carrier.",
  },
  // FM per-operator effective frequency LCD (driven by MUL × note, MUL float, or fixed Hz).
  "freq_lcd": {
    name: "OP FREQUENCY",
    desc: "Effective frequency of the operator — integer multiplier, float multiplier or fixed Hz depending on FREQ CTRL mode.",
  },
  // Header L/R output meters (driven by `meterData` backend event).
  "level_meter_l": {
    name: "OUTPUT L",
    desc: "Peak level of the left output channel after master volume. Top two segments redline.",
  },
  "level_meter_r": {
    name: "OUTPUT R",
    desc: "Peak level of the right output channel after master volume. Top two segments redline.",
  },
  // Header patch-name LCD (also the patch nav anchor).
  "patch_lcd": {
    name: "PATCH NAME",
    desc: "Currently loaded preset for the active mode. Reads AUDIO FX in D mode and EMPTY when no preset is loaded.",
  },
  // Header NOTE ON LED + caption.
  "note_on": {
    name: "NOTE ON",
    desc: "Lights when any voice (FM or SQ) is currently sounding. Useful as a quick MIDI-routing check.",
  },

  // --- Gallery scratch params ---------------------------------------
  "gallery_knob_a": {
    name: "GALLERY KNOB A",
    desc: "Scratch knob param wired only for widget-gallery testing.",
  },
  "gallery_knob_b": {
    name: "GALLERY KNOB B",
    desc: "Scratch knob param wired only for widget-gallery testing.",
  },
  "gallery_knob_c": {
    name: "GALLERY KNOB C",
    desc: "Scratch knob param wired only for widget-gallery testing.",
  },
  "gallery_knob_d": {
    name: "GALLERY KNOB D",
    desc: "Scratch knob param wired only for widget-gallery testing.",
  },
  "gallery_toggle_a": {
    name: "GALLERY TOGGLE A",
    desc: "Scratch boolean param for testing the toggle-switch widget.",
  },
  "gallery_combo_a": {
    name: "GALLERY COMBO A",
    desc: "Scratch enumerated param for testing the combo-bound widgets.",
  },
  "gallery_algo": {
    name: "GALLERY ALGORITHM",
    desc: "Scratch 0..7 param driving both the algo-grid and algorithm-mini in the gallery.",
  },
  "gallery_stepper": {
    name: "GALLERY STEPPER",
    desc: "Scratch integer param for testing the stepper widget's click-and-hold.",
  },
  "gallery_level": {
    name: "GALLERY LEVEL",
    desc: "Scratch 0..1 param driving the level-meter for visual verification.",
  },
  "gallery_noteon": {
    name: "GALLERY NOTE ON",
    desc: "Scratch boolean param that lights the note-on LED in the gallery.",
  },
  "gallery_wheel": {
    name: "GALLERY WHEEL",
    desc: "Scratch 0..1 param exercising both midi-wheel variants in the gallery.",
  },
};

// ---------------------------------------------------------------------------
// Per-mode panel entries — generated at module load.
//
// Tasks 05/06 wired the per-op FM widgets and per-channel SQ widgets but
// landed without filling in tooltip entries, so every `tipId` like
// `ar_op1` or `psg_atk_ch1` hit `applyTooltip` with no match and the
// host element ended up with no `data-tip-*` attrs. The hover handler
// then ignored those elements ("Tips toggle doesn't work on all
// controls"). We generate them here from base tables so adding a new
// per-op or per-channel widget doesn't require hand-touching this file.
// ---------------------------------------------------------------------------

const FM_OP_BASE = {
  tl:            { short: "TL",         desc: "Total level — the operator's loudness. Carriers hear this as volume; modulators hear it as modulation index. 0 silent, max loud." },
  dt:            { short: "DT",         desc: "Detune — small frequency offset (~±3 cents in 7 steps). Use sparingly for chorus/beating against unison voices." },
  mul:           { short: "MUL",        desc: "Frequency multiplier (0.5×, 1×..15×) of the keyed note. Used in INT MUL mode." },
  ks:            { short: "RS",         desc: "Rate-scaling — speeds up the envelope for higher-pitched notes. 0 disables, 3 maximally tracks." },
  ar:            { short: "AR",         desc: "Attack rate — how quickly the envelope rises from key-on to full level. 0 slowest, 31 instant." },
  dr:            { short: "DR",         desc: "Decay rate — how quickly the envelope falls from peak to the sustain level. 0 hold peak, 31 snap." },
  sr:            { short: "SR",         desc: "Secondary decay rate — slope during the sustained portion. 0 flat hold, 31 falls toward zero." },
  rr:            { short: "RR",         desc: "Release rate — how quickly the envelope falls after key-off. 0 long tail, 15 instant cut." },
  sl:            { short: "SL",         desc: "Sustain level — the floor reached after the decay segment. 0 silent, max full." },
  ssg:           { short: "SSG-EG",     desc: "SSG-EG envelope shape (OFF + 8 named shapes). SDR=saw down repeat, SDO=saw down once, ALT=alternate (triangle), SDH=saw down then hold, SUR=saw up repeat, SUH=saw up then hold, ALU=alternate up, SUO=saw up once. Most patches leave at OFF." },
  amon:          { short: "AM ON",      desc: "Enable amplitude-modulation routing of the global LFO into this operator's TL. Off = static loudness." },
  mul_float:     { short: "MUL FLOAT",  desc: "Per-operator float frequency multiplier. Active in FLOAT MUL and AUTO RETRIG modes." },
  fixed:         { short: "FIXED",      desc: "Lock this operator to a fixed Hz (set on FREQ FIXED). Active in FLOAT MUL and AUTO RETRIG modes." },
  freq_fixed_hz: { short: "FREQ FIXED", desc: "Fixed frequency in Hz when this operator's FIXED toggle is on. Ignored otherwise." },
  vel:           { short: "VEL",        desc: "Per-operator MIDI velocity scaling depth — adds extra TL attenuation on softer notes. 0 = no velocity reaction." },
};

for (let op = 1; op <= 4; ++op) {
  for (const [key, entry] of Object.entries(FM_OP_BASE)) {
    TOOLTIPS[`${key}_op${op}`] = {
      name: `${entry.short} OP${op}`,
      desc: entry.desc,
    };
  }
  TOOLTIPS[`op_badge_${op}`] = {
    name: `OP ${op} FOCUS`,
    desc: `Click to focus the envelope display on operator ${op}. Touching any operator-${op} knob also switches the envelope to this row.`,
  };
}

// FM part-level + v2 globals.
Object.assign(TOOLTIPS, {
  alg:              { name: "ALGORITHM",      desc: "YM2612 algorithm 1..8 — determines which operators are carriers vs modulators." },
  fb:               { name: "OP1 FB",         desc: "Self-feedback amount on operator 1. 0 none, 7 most. Higher feedback = brighter / noisier carrier." },
  ams:              { name: "AMS",            desc: "LFO amplitude-modulation depth — multiplies every operator's AM-ON contribution." },
  pms:              { name: "PMS",            desc: "LFO frequency-modulation depth — global vibrato amount; mod wheel and aftertouch layer on top." },
  lr:               { name: "L/R ROUTING",    desc: "Hardware L/R routing — 0 OFF, 1 L, 2 R, 3 BOTH. Patches set this; OFF silences FM output." },
  lfo_enable:       { name: "LFO ENABLE",     desc: "Global FM LFO on/off. When off, RATE / PMS / AMS have no effect." },
  lfo_rate:         { name: "LFO RATE",       desc: "Global FM LFO frequency, 0..7 (~3.85..72.18 Hz)." },
  channel_tl:       { name: "CHANNEL TL",     desc: "Channel-wide TL trim — scales every operator's contribution together. Use as a fast mute / overall level." },
  mod_wheel_value:  { name: "MOD WHEEL",      desc: "Live MIDI mod wheel mirror. Layered onto PMS as additional vibrato depth." },
  pitch_bend_value: { name: "PITCH BEND",     desc: "Live MIDI pitch wheel mirror. Range set by PB RANGE in the LFO/Global block." },
  note_mode:        { name: "NOTE MODE",      desc: "RETRIG re-keys the envelope on every overlapping note; LEGATO keeps the envelope going and just updates pitch." },
  poly_voices:      { name: "POLY",           desc: "Active voice count, 1..16. 1 = mono (LEGATO toggle applies). Hardware Strict caps at 6." },
  pitch_bend_range: { name: "PB RANGE",       desc: "Pitch-bend depth in semitones, 1..12." },
  freq_ctrl_mode:   { name: "FREQ CTRL MODE", desc: "INT MUL = standard YM2612; FLOAT MUL = ch3 special with per-op float pitch; AUTO RETRIG = CSM+TimerA retriggering." },
  retrig_rate:      { name: "RETRIG RATE",    desc: "TimerA rate for AUTO RETRIG mode — how fast operators retrigger. Visible only in AUTO RETRIG." },
});

// SQ per-channel envelope + mix params. The shared envelope label mapping
// (ATK→AR, DR1→DR, SUS→SL, DR2→SR, RR→RR) matches the FM envelope rendering
// in sq-view.js.
const SQ_BASE = {
  atk: { name: "ATK", desc: "Attack rate of the channel's envelope. 0 slowest, 31 instant." },
  dr1: { name: "DR1", desc: "First decay rate — falls from peak to the SUS floor." },
  sus: { name: "SUS", desc: "Sustain level — the floor between the two decay segments. 0 silent, 15 full." },
  dr2: { name: "DR2", desc: "Second decay rate — slope during the sustained portion." },
  rr:  { name: "RR",  desc: "Release rate — applied after key-off. 0 long tail, 15 instant." },
  vel: { name: "VEL", desc: "MIDI velocity scaling depth for this channel. 0 = no velocity reaction." },
  vol: { name: "VOL", desc: "Channel mixer level — applied after the envelope, before the master VU." },
  pan: { name: "PAN", desc: "Stereo position. -1 hard left, 0 centre, +1 hard right." },
};
const SQ_TONE_EXTRA = {
  detune: { name: "DETUNE", desc: "Pitch offset in cents (-100..+100). Tone channels only — noise has no pitch." },
  glide:  { name: "GLIDE",  desc: "Portamento time in ms when stepping between overlapping notes. 0 disables." },
};
const SQ_CHANNELS = [
  { sfx: "ch1",   label: "TONE 1" },
  { sfx: "ch2",   label: "TONE 2" },
  { sfx: "ch3",   label: "TONE 3" },
  { sfx: "noise", label: "NOISE"  },
];
for (const { sfx, label } of SQ_CHANNELS) {
  for (const [k, entry] of Object.entries(SQ_BASE)) {
    TOOLTIPS[`psg_${k}_${sfx}`] = {
      name: `${label} ${entry.name}`,
      desc: entry.desc,
    };
  }
}
// Detune + glide live on tone channels only — noise has no pitch in the
// SN76489.
for (let i = 0; i < 3; ++i) {
  const { sfx, label } = SQ_CHANNELS[i];
  for (const [k, entry] of Object.entries(SQ_TONE_EXTRA)) {
    TOOLTIPS[`psg_${k}_${sfx}`] = {
      name: `${label} ${entry.name}`,
      desc: entry.desc,
    };
  }
}
Object.assign(TOOLTIPS, {
  psg_noise_type: { name: "NOISE TYPE", desc: "Noise generator mode — W (white, random LFSR) or P (periodic, short looping pattern)." },
  psg_noise_rate: { name: "NOISE RATE", desc: "Noise clock divider — L/M/H presets, or CH2 to track tone channel 2's frequency for tuned noise." },
  psg_noise_auto: { name: "NOISE AUTO", desc: "When on, the noise channel automatically follows tone channel 2's pitch (locked to RATE = CH2)." },
});

// Helper used by each widget mount(): writes data-tip-name / data-tip-desc
// onto the control's host element if a tooltip entry exists. Tolerant of
// unknown ids — the per-mode panels (Tasks 05-07) will add their own
// entries.
export function applyTooltip(host, tipId) {
  if (!tipId) return;
  const entry = TOOLTIPS[tipId];
  if (!entry) return;
  // Strip any native title attribute so the OS-level tooltip can't race the
  // hover handler — only one descriptor should reach the user.
  if (host.removeAttribute) host.removeAttribute("title");
  host.setAttribute("data-tip-name", entry.name);
  host.setAttribute("data-tip-desc", entry.desc);
}
