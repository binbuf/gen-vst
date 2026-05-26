// `patch-name-lcd` widget — large LCD readout for the header patch name.
//
// Larger sibling of `lcd-readout`. Default size matches the .lcd-patch
// recipe in design-system.css (≈ 240×34); font follows the
// .t-patch-lcd typography utility (18 px IBM Plex Mono Medium with heavier
// bloom).

import { mount as mountLcd } from "./lcd-readout.js";

// `opts.tipId` is forwarded to lcd-readout via the spread, which already
// supports tipId (lcd-readout.js applies it on its host). No extra wiring
// needed here — the caller passes { tipId: "patch_lcd" } and the tooltip
// data-attrs land on the same host element.
export function mount(host, opts = {}) {
  return mountLcd(host, {
    width: 260,
    height: 34,
    fontPx: 18,
    align: "center",
    initialText: "— EMPTY —",
    ...opts,
  });
}
