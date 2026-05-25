// `decimator-knob` widget — the PCM2612-style large central knob.
//
// Pure thin wrapper around `knob.js` with the visual variant baked in:
// 96 px size, matte body (no top sheen). The CSS recipe in
// design-system.css already styles `.knob.decimator-knob`; we just pass the
// modifier class through.

import { mount as mountKnob } from "./knob.js";

export function mount(host, opts = {}) {
  return mountKnob(host, {
    ...opts,
    size: opts.size ?? 96,
    extraClass: `decimator-knob ${opts.extraClass ?? ""}`.trim(),
  });
}
