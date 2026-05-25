// `toggle-switch` widget — CSS-only on/off toggle.
//
// Two flavours, gated by opts.variant:
//   - "single" (default) — a 28 px square .toggle that lights when on.
//   - "two-way"          — a .switch-2way with two labelled positions
//                          (e.g. LEGACY / CRYSTAL CLEAR). One option is
//                          always active.
//
// Both bind to a bool apvts param via bindToggle. The two-way variant maps
// position-0 to false and position-1 to true.

import { applyTooltip } from "./tooltip-content.js";

function mountSingle(host, { bind, tipId }) {
  host.classList.add("toggle");
  if (tipId) applyTooltip(host, tipId);

  const handleClick = () => {
    if (bind) bind.set(!bind.get());
    else      host.classList.toggle("is-on");
  };
  host.addEventListener("click", handleClick);

  let unsub = null;
  if (bind) {
    unsub = bind.onChange((on) => host.classList.toggle("is-on", Boolean(on)));
  }

  return {
    setActive(on) {
      if (bind) bind.set(Boolean(on));
      else      host.classList.toggle("is-on", Boolean(on));
    },
    isActive() { return host.classList.contains("is-on"); },
    dispose() {
      host.removeEventListener("click", handleClick);
      if (unsub) unsub();
    },
  };
}

function mountTwoWay(host, { bind, labels, tipId }) {
  host.classList.add("switch-2way");
  host.innerHTML = "";
  if (tipId) applyTooltip(host, tipId);

  const [labA, labB] = labels ?? ["OFF", "ON"];
  const posA = document.createElement("div");
  const posB = document.createElement("div");
  posA.className = "switch-pos";
  posB.className = "switch-pos";
  posA.textContent = labA;
  posB.textContent = labB;
  host.appendChild(posA);
  host.appendChild(posB);

  const apply = (on) => {
    posA.classList.toggle("is-active", !on);
    posB.classList.toggle("is-active",  on);
  };

  const onA = () => { if (bind) bind.set(false); else apply(false); };
  const onB = () => { if (bind) bind.set(true);  else apply(true);  };
  posA.addEventListener("click", onA);
  posB.addEventListener("click", onB);

  let unsub = null;
  if (bind) unsub = bind.onChange((v) => apply(Boolean(v)));
  else      apply(false);

  return {
    setActive(on) { if (bind) bind.set(Boolean(on)); else apply(Boolean(on)); },
    isActive() { return posB.classList.contains("is-active"); },
    dispose() {
      posA.removeEventListener("click", onA);
      posB.removeEventListener("click", onB);
      if (unsub) unsub();
    },
  };
}

export function mount(host, opts = {}) {
  if (opts.variant === "two-way") {
    return mountTwoWay(host, opts);
  }
  return mountSingle(host, opts);
}
