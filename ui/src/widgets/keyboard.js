// On-screen piano keyboard strip.
//
// Draws 7 octaves (MIDI 24 = C1 through 107 = B7) as a Canvas-rendered
// keyboard. Lit keys show incoming MIDI from the `meterData` activeNotes
// array; clicking keys injects synthetic notes via the `noteOn`/`noteOff`
// native functions.
//
// Usage:
//   import { mount } from "./widgets/keyboard.js";
//   const disposer = mount(containerEl);
//   disposer.dispose(); // cleanup listeners

import { onBackendEvent, bindToggle } from "../binding.js";
import { getNativeFunction } from "../juce/index.js";

// Key layout constants — all sizes in CSS pixels at 1× scale.
const FIRST_NOTE    = 24;   // C1
const LAST_NOTE     = 107;  // B7
const NUM_OCTAVES   = 7;
const WHITE_PER_OCT = 7;
const NUM_WHITE     = NUM_OCTAVES * WHITE_PER_OCT; // 49

// Chromatic note → white key index within octave (null = black key).
const WHITE_IDX = [0, null, 1, null, 2, 3, null, 4, null, 5, null, 6];

// Chromatic note → black key left-edge offset in white-key-widths from octave start.
// These reproduce standard piano black-key proportions.
const BLACK_X_FRAC = [null, 0.62, null, 1.64, null, null, 3.61, null, 4.62, null, 5.63, null];

function isBlack(pitch) {
  const n = (pitch - FIRST_NOTE + 12 * 10) % 12;
  return WHITE_IDX[n] === null;
}

// Compute drawing geometry for a given pitch from layout constants.
function keyGeometry(pitch, whiteW, whiteH, blackW, blackH, marginX) {
  const relPitch = pitch - FIRST_NOTE;
  const octave   = Math.floor(relPitch / 12);
  const n        = relPitch % 12;
  const octX     = marginX + octave * WHITE_PER_OCT * whiteW;

  if (WHITE_IDX[n] !== null) {
    return { x: octX + WHITE_IDX[n] * whiteW, w: whiteW, h: whiteH, isWhite: true };
  } else {
    const bx = octX + BLACK_X_FRAC[n] * whiteW;
    return { x: bx, w: blackW, h: blackH, isWhite: false };
  }
}

// Hit-test a canvas coordinate; returns MIDI pitch or null.
function pitchAtPoint(px, py, whiteW, whiteH, blackW, blackH, marginX) {
  // Check black keys first (they render on top).
  for (let p = FIRST_NOTE; p <= LAST_NOTE; p++) {
    if (!isBlack(p)) continue;
    const g = keyGeometry(p, whiteW, whiteH, blackW, blackH, marginX);
    if (px >= g.x && px < g.x + g.w && py < g.h) return p;
  }
  for (let p = FIRST_NOTE; p <= LAST_NOTE; p++) {
    if (isBlack(p)) continue;
    const g = keyGeometry(p, whiteW, whiteH, blackW, blackH, marginX);
    if (px >= g.x && px < g.x + g.w && py < g.h) return p;
  }
  return null;
}

// Colour palette — mirrors design-system.css tokens.
const CLR_WHITE_KEY    = "#d8dce0";
const CLR_WHITE_BORDER = "#0a0c10";
const CLR_BLACK_KEY    = "#1c1f24";
const CLR_BLACK_BORDER = "#080a0d";
const CLR_WHITE_LIT    = "#5ad4f8";
const CLR_BLACK_LIT    = "#2196f3";
const CLR_WHITE_PRESS  = "#aed7f0";
const CLR_BLACK_PRESS  = "#1565c0";

export function mount(container) {
  const canvas = document.createElement("canvas");
  canvas.style.cssText = "display:block;width:100%;height:100%;cursor:pointer;";
  container.appendChild(canvas);

  let activeNotes = new Set();
  let pressedPitch = null;
  let nativeFns = null;

  // Lazy-initialise native functions once the JUCE backend is present.
  function ensureNativeFns() {
    if (nativeFns) return nativeFns;
    if (!window.__JUCE__) return null;
    try {
      nativeFns = {
        noteOn:  getNativeFunction("noteOn"),
        noteOff: getNativeFunction("noteOff"),
      };
    } catch (_) {
      // Running in browser dev without JUCE — no-op.
    }
    return nativeFns;
  }

  // Layout derived from the canvas's physical pixel size.
  function layout() {
    const w = canvas.width;
    const h = canvas.height;
    const whiteW  = Math.floor(w / NUM_WHITE);
    const marginX = Math.floor((w - whiteW * NUM_WHITE) / 2);
    const whiteH  = h;
    const blackW  = Math.max(6, Math.round(whiteW * 0.58));
    const blackH  = Math.round(whiteH * 0.60);
    return { whiteW, whiteH, blackW, blackH, marginX };
  }

  function draw() {
    const dpr = window.devicePixelRatio || 1;
    const rect = canvas.getBoundingClientRect();
    if (rect.width === 0 || rect.height === 0) return;

    if (canvas.width  !== Math.round(rect.width  * dpr) ||
        canvas.height !== Math.round(rect.height * dpr)) {
      canvas.width  = Math.round(rect.width  * dpr);
      canvas.height = Math.round(rect.height * dpr);
    }

    const ctx = canvas.getContext("2d");
    const { whiteW, whiteH, blackW, blackH, marginX } = layout();

    ctx.clearRect(0, 0, canvas.width, canvas.height);

    // Draw white keys first.
    for (let p = FIRST_NOTE; p <= LAST_NOTE; p++) {
      if (isBlack(p)) continue;
      const g = keyGeometry(p, whiteW, whiteH, blackW, blackH, marginX);
      const lit = activeNotes.has(p);
      const pressed = pressedPitch === p;

      ctx.fillStyle = pressed ? CLR_WHITE_PRESS : lit ? CLR_WHITE_LIT : CLR_WHITE_KEY;
      ctx.fillRect(g.x, 0, g.w, g.h);

      // Border between white keys (right edge only, except last).
      ctx.fillStyle = CLR_WHITE_BORDER;
      ctx.fillRect(g.x + g.w - 1, 0, 1, g.h);

      // Octave C label — small, dim.
      const n = (p - FIRST_NOTE) % 12;
      if (n === 0) {
        const octNum = Math.floor((p - FIRST_NOTE) / 12) + 1;
        ctx.fillStyle = "rgba(42,44,48,0.55)";
        ctx.font = `${Math.max(7, Math.round(whiteW * 0.45))}px ui-monospace, 'IBM Plex Mono', monospace`;
        ctx.textAlign = "center";
        ctx.fillText(`C${octNum}`, g.x + g.w / 2, g.h - 4);
      }
    }

    // Draw black keys on top.
    for (let p = FIRST_NOTE; p <= LAST_NOTE; p++) {
      if (!isBlack(p)) continue;
      const g = keyGeometry(p, whiteW, whiteH, blackW, blackH, marginX);
      const lit = activeNotes.has(p);
      const pressed = pressedPitch === p;

      ctx.fillStyle = CLR_BLACK_BORDER;
      ctx.fillRect(g.x - 1, 0, g.w + 2, g.h + 2);

      ctx.fillStyle = pressed ? CLR_BLACK_PRESS : lit ? CLR_BLACK_LIT : CLR_BLACK_KEY;
      ctx.fillRect(g.x, 0, g.w, g.h);

      // Subtle highlight on the top edge of black key.
      ctx.fillStyle = "rgba(255,255,255,0.10)";
      ctx.fillRect(g.x + 1, 0, g.w - 2, 2);
    }
  }

  // ---- Event handlers -------------------------------------------------------

  function canvasXY(e) {
    const rect = canvas.getBoundingClientRect();
    const dpr  = window.devicePixelRatio || 1;
    return {
      x: (e.clientX - rect.left) * dpr,
      y: (e.clientY - rect.top)  * dpr,
    };
  }

  function getLayout() { return layout(); }

  function handleDown(e) {
    e.preventDefault();
    const { x, y } = canvasXY(e);
    const { whiteW, whiteH, blackW, blackH, marginX } = getLayout();
    const p = pitchAtPoint(x, y, whiteW, whiteH, blackW, blackH, marginX);
    if (p === null) return;
    pressedPitch = p;
    const fns = ensureNativeFns();
    if (fns) fns.noteOn(p, 100);
    draw();
  }

  function handleMove(e) {
    if (pressedPitch === null) return;
    e.preventDefault();
    const { x, y } = canvasXY(e);
    const { whiteW, whiteH, blackW, blackH, marginX } = getLayout();
    const p = pitchAtPoint(x, y, whiteW, whiteH, blackW, blackH, marginX);
    if (p === pressedPitch) return;
    const fns = ensureNativeFns();
    if (fns) {
      fns.noteOff(pressedPitch);
      if (p !== null) fns.noteOn(p, 100);
    }
    pressedPitch = p;
    draw();
  }

  function handleUp() {
    if (pressedPitch === null) return;
    const fns = ensureNativeFns();
    if (fns) fns.noteOff(pressedPitch);
    pressedPitch = null;
    draw();
  }

  canvas.addEventListener("mousedown",  handleDown);
  canvas.addEventListener("mousemove",  handleMove);
  canvas.addEventListener("mouseup",    handleUp);
  canvas.addEventListener("mouseleave", handleUp);

  // Prevent context menu from interrupting a held note.
  canvas.addEventListener("contextmenu", (e) => e.preventDefault());

  // ---- Backend event subscription ------------------------------------------

  const unsubMeter = onBackendEvent("meterData", (payload) => {
    const notes = payload.activeNotes;
    const next = new Set(Array.isArray(notes) ? notes : []);
    // Only redraw when the set actually changes.
    let changed = next.size !== activeNotes.size;
    if (!changed) for (const n of next) if (!activeNotes.has(n)) { changed = true; break; }
    if (changed) { activeNotes = next; draw(); }
  });

  // ---- Keyboard visibility binding -----------------------------------------
  // Mirrors the `keyboard_visible` apvts bool to the strip container's
  // display property. C++ resizes the window in parallel so no empty space
  // is left behind when the strip is hidden.

  let visibilityBind = null;
  try {
    visibilityBind = bindToggle("keyboard_visible");
    visibilityBind.onChange((visible) => {
      if (!visible && pressedPitch !== null) handleUp(); // release held note
      container.style.display = visible ? "" : "none";
    });
  } catch (_) {
    // Running outside JUCE (dev/gallery) — keyboard always visible.
  }

  // ---- Initial draw + resize observer --------------------------------------

  draw();

  let resizeObs = null;
  if (typeof ResizeObserver !== "undefined") {
    resizeObs = new ResizeObserver(() => draw());
    resizeObs.observe(container);
  }

  return {
    dispose() {
      unsubMeter();
      if (visibilityBind) visibilityBind.dispose();
      canvas.removeEventListener("mousedown",  handleDown);
      canvas.removeEventListener("mousemove",  handleMove);
      canvas.removeEventListener("mouseup",    handleUp);
      canvas.removeEventListener("mouseleave", handleUp);
      if (resizeObs) resizeObs.disconnect();
      canvas.remove();
    },
  };
}
