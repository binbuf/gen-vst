// Strips developer-facing WebView affordances from the embedded bundle so
// end users don't see the browser-style right-click menu, inspector
// shortcuts, zoom shortcuts, or reload shortcuts that WebView2 / WKWebView
// honour by default.
//
// Imported lazily from main.js when `import.meta.env.PROD` is true — so the
// dev server (GENVST_DEV_SERVER=ON, vite dev) never bundles or fetches this
// module and devtools stay fully functional during development. Idempotent.

const FUNCTION_KEYS_TO_SWALLOW = new Set([
  "F5",   // reload
  "F11",  // inspector / fullscreen
  "F12",  // inspector
]);

const CTRL_LETTERS_TO_SWALLOW = new Set([
  "r",    // reload
  "p",    // print
  "u",    // view-source
  "s",    // save-page
]);

const CTRL_SHIFT_LETTERS_TO_SWALLOW = new Set([
  "i",    // inspector
  "j",    // console
  "c",    // element picker
]);

const CTRL_ZOOM_KEYS = new Set([
  "+", "-", "=", "_", "0",
]);

export function install() {
  if (window.__genvstGuardInstalled__) return;
  window.__genvstGuardInstalled__ = true;

  window.addEventListener("contextmenu", (e) => e.preventDefault(),
    { capture: true });

  window.addEventListener("keydown", (e) => {
    if (FUNCTION_KEYS_TO_SWALLOW.has(e.key)) { e.preventDefault(); return; }

    const mod = e.ctrlKey || e.metaKey;
    if (!mod) return;

    const k = e.key.toLowerCase();
    if (CTRL_LETTERS_TO_SWALLOW.has(k))             { e.preventDefault(); return; }
    if (e.shiftKey && CTRL_SHIFT_LETTERS_TO_SWALLOW.has(k))
                                                    { e.preventDefault(); return; }
    if (CTRL_ZOOM_KEYS.has(e.key))                  { e.preventDefault(); return; }
  }, { capture: true });

  window.addEventListener("wheel", (e) => {
    if (e.ctrlKey || e.metaKey) e.preventDefault();
  }, { capture: true, passive: false });

  window.addEventListener("dragstart", (e) => e.preventDefault(),
    { capture: true });
}
