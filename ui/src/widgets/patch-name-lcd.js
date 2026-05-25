// `patch-name-lcd` widget — large LCD readout for the header patch name.
//
// Larger sibling of `lcd-readout`. Default size matches the .lcd-patch
// recipe in design-system.css (≈ 240×34); font follows the
// .t-patch-lcd typography utility (18 px IBM Plex Mono Medium with heavier
// bloom).

import { mount as mountLcd } from "./lcd-readout.js";

export function mount(host, opts = {}) {
  return mountLcd(host, {
    width: 240,
    height: 34,
    fontPx: 18,
    align: "center",
    initialText: "— EMPTY —",
    ...opts,
  });
}
