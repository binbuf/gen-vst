// FM mode host (ADR-0021 amendment). Mounts either the normal FM patch panel
// (fm-view) or the drum-kit pad grid (fm-kit-view) into the mode panel, and
// swaps between them: the FM panel's KIT button enters kit mode, the kit
// panel's "FM PATCH" button (or a kit being deactivated) leaves it.
//
// It also reconciles with the engine on mount and whenever a preset is loaded
// (the C++ `patchLoaded` event): loading a `.gnkit` from the browser swaps to
// the kit view; loading a single patch swaps back. Re-mounts only happen on an
// actual mode change so editing within a view is never interrupted.

import { getNativeFunction }          from "../juce/index.js";
import { onBackendEvent }             from "../binding.js";
import { mount as mountFmView }       from "./fm-view.js";
import { mount as mountFmKitView }    from "./fm-kit-view.js";

export function mount(root) {
  let disposer = null;
  let disposed = false;
  let currentIsKit = null;   // null = nothing mounted yet

  const show = (kitMode) => {
    if (disposed) return;
    if (kitMode === currentIsKit) return;   // already in the right view
    if (disposer) { disposer.dispose(); disposer = null; }
    currentIsKit = kitMode;
    if (kitMode)
      disposer = mountFmKitView(root, { onExit: () => show(false) });
    else
      disposer = mountFmView(root, { onEnterKit: () => show(true) });
  };

  const reconcile = () => {
    try {
      const getKit = getNativeFunction("getKit");
      if (getKit)
        getKit().then((r) => { if (!disposed) show(!!(r && r.active)); });
    } catch (_e) { /* native bridge unavailable — stay on patch view */ }
  };

  // Patch view first, then reconcile with the engine's actual kit state.
  show(false);
  reconcile();

  // A preset load (single patch or `.gnkit`) can flip kit state — re-check.
  let unsubPatch = () => {};
  try { unsubPatch = onBackendEvent("patchLoaded", () => reconcile()); }
  catch (_e) { /* no backend (gallery / dev) */ }

  return {
    dispose() {
      disposed = true;
      try { unsubPatch(); } catch (_e) { /* ignore */ }
      if (disposer) { disposer.dispose(); disposer = null; }
    },
  };
}
