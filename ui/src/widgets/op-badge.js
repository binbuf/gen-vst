// `op-badge` widget — blue numbered square for operator 1..4 selection.
//
// Pure CSS chassis (.op-badge recipe). JS owns:
//   - The numeral text content (opts.index in [1..4]).
//   - The .is-active flag, toggled by setActive(bool) from the parent panel
//     to indicate which operator the envelope-curve widget tracks.
//   - The click handler invokes opts.onClick(index) so the parent FM panel
//     can flip its active-op state.
//
// op-badge.active is local UI state — NOT an apvts param (05-ui-ux.md
// *Component Inventory*).

import { applyTooltip } from "./tooltip-content.js";

export function mount(host, opts = {}) {
  const { index = 1, onClick = null, tipId = null } = opts;

  host.classList.add("op-badge");
  host.textContent = String(index);
  if (tipId) applyTooltip(host, tipId);

  const handleClick = () => { if (onClick) onClick(index); };
  host.addEventListener("click", handleClick);

  return {
    setActive(on) { host.classList.toggle("is-active", Boolean(on)); },
    isActive() { return host.classList.contains("is-active"); },
    getIndex() { return index; },
    dispose() {
      host.removeEventListener("click", handleClick);
    },
  };
}
