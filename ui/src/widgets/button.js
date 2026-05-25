// `button` widget — pill / square button.
//
// Pure CSS (.btn recipe in design-system.css); JS handles binding semantics:
//   - If `opts.bind` is a bindToggle controller, clicking the button flips
//     the bound boolean and the button reflects state via .is-active.
//   - Otherwise the widget is fire-only — `opts.onClick(ev)` is invoked on
//     each press; no bound state.
//
// Press feedback (scale + inset shadow) comes from :active in the CSS recipe.

import { applyTooltip } from "./tooltip-content.js";

export function mount(host, opts = {}) {
  const {
    bind = null,         // bindToggle controller; optional
    label = null,        // text override; falls back to host's existing text
    onClick = null,      // fire-only handler when no bind
    tipId = null,
  } = opts;

  host.classList.add("btn");
  if (label != null) host.textContent = label;
  if (tipId) applyTooltip(host, tipId);
  // Make sure clicks have a sensible default cursor / type if rendered onto
  // a <span> or <div>. Buttons rendered onto <button> elements are fine.
  if (host.tagName !== "BUTTON") host.setAttribute("role", "button");

  const handleClick = (e) => {
    if (bind) {
      bind.set(!bind.get());
    } else if (onClick) {
      onClick(e);
    }
  };
  host.addEventListener("click", handleClick);

  let unsub = null;
  if (bind) {
    unsub = bind.onChange((on) => {
      host.classList.toggle("is-active", Boolean(on));
    });
  }

  return {
    setActive(on) {
      if (bind) bind.set(Boolean(on));
      else      host.classList.toggle("is-active", Boolean(on));
    },
    isActive() { return host.classList.contains("is-active"); },
    dispose() {
      host.removeEventListener("click", handleClick);
      if (unsub) unsub();
    },
  };
}
