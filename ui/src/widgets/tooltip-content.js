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

  // --- D mode panel --------------------------------------------------
  "dry_wet": {
    name: "DRY / WET",
    desc: "Blend between unprocessed input (0) and fully decimated signal (1).",
  },
  "mono": {
    name: "MONO",
    desc: "Sum L+R after decimation so both output channels carry the same signal.",
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
