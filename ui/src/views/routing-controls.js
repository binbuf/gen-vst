/*
 * Routing controls — the inline `MIDI` step-fields on views 1/2/3 and the
 * routing modal (view 5) all edit the same MidiRouter table via the
 * getRouting/setRouting/resetRouting native functions. The destinations
 * (FM parts, PSG tones, PSG noise, DAC) are identified by { kind, index }
 * across the JS↔C++ boundary.
 *
 * Step-field semantics: shows the MIDI channel currently routed to the given
 * destination (1..16) or "OFF" (0). Clicking up/down sends setRouting() and
 * (because the JS routing-state notifies listeners) every step-field bound
 * to a related destination repaints (so a conflict warning in the routing
 * modal stays in sync).
 */

import * as Juce from "../juce/index.js";
import {
  setupPixelCanvas, palette, drawBevel, drawLedReadout,
  GLYPH_H, readoutWidth,
} from "../widgets/pixel.js";

const ARROW_W = 9;
const ARROW_H = 7;
const PAD = 3;
const WIDTH_CHARS = 3;          // up to "OFF" (3 chars) or "16"

const getRoutingFn  = Juce.getNativeFunction("getRouting");
const setRoutingFn  = Juce.getNativeFunction("setRouting");
const resetRoutingFn = Juce.getNativeFunction("resetRouting");

/* Singleton routing state — fetched once from C++ on first use, kept in sync
 * by setRoutingChannel() calls. Listeners get notified on every change so the
 * modal table + every inline step-field stay coherent. */
const STATE = {
  table: null,    // { fmParts:[16], psgTones:[16], psgNoise:16, dac:16 }
  fetched: false,
  fetching: null,
  listeners: new Set(),
};

function defaultTable() {
  return {
    fmParts:   [1, 2, 3, 4, 5, 6],
    psgTones:  [11, 12, 13],
    psgNoise:  14,
    dac:       16,
  };
}

function notify() {
  for (const fn of STATE.listeners) {
    try { fn(STATE.table); } catch (e) { /* ignore */ }
  }
}

export function fetchRouting() {
  if (STATE.fetched) return Promise.resolve(STATE.table);
  if (STATE.fetching) return STATE.fetching;
  STATE.fetching = getRoutingFn().then((resp) => {
    STATE.table = normaliseTable(resp);
    STATE.fetched = true;
    STATE.fetching = null;
    notify();
    return STATE.table;
  }).catch(() => {
    STATE.table = defaultTable();
    STATE.fetched = true;
    STATE.fetching = null;
    notify();
    return STATE.table;
  });
  return STATE.fetching;
}

function normaliseTable(resp) {
  const t = defaultTable();
  if (resp && typeof resp === "object") {
    if (Array.isArray(resp.fmParts)) {
      for (let i = 0; i < Math.min(6, resp.fmParts.length); ++i)
        t.fmParts[i] = clampChannel(resp.fmParts[i]);
    }
    if (Array.isArray(resp.psgTones)) {
      for (let i = 0; i < Math.min(3, resp.psgTones.length); ++i)
        t.psgTones[i] = clampChannel(resp.psgTones[i]);
    }
    if (resp.psgNoise !== undefined) t.psgNoise = clampChannel(resp.psgNoise);
    if (resp.dac      !== undefined) t.dac      = clampChannel(resp.dac);
  }
  return t;
}

function clampChannel(v) {
  const n = Math.round(Number(v));
  if (!Number.isFinite(n)) return 0;
  if (n < 0 || n > 16) return 0;
  return n;
}

export function onRoutingChange(fn) {
  STATE.listeners.add(fn);
  return () => STATE.listeners.delete(fn);
}

export function getChannelForDestination(dest) {
  const t = STATE.table ?? defaultTable();
  switch (dest.kind) {
    case "fm":        return t.fmParts[dest.index] ?? 0;
    case "psg-tone":  return t.psgTones[dest.index] ?? 0;
    case "psg-noise": return t.psgNoise ?? 0;
    case "dac":       return t.dac ?? 0;
    default:          return 0;
  }
}

export function setChannelForDestination(dest, channel) {
  const t = STATE.table ?? defaultTable();
  const c = clampChannel(channel);
  switch (dest.kind) {
    case "fm":        t.fmParts[dest.index] = c; break;
    case "psg-tone":  t.psgTones[dest.index] = c; break;
    case "psg-noise": t.psgNoise = c; break;
    case "dac":       t.dac      = c; break;
    default: return;
  }
  STATE.table = t;
  notify();
  setRoutingFn(dest.kind, dest.index ?? 0, c).catch(() => {});
}

export function resetRoutingToDefaults() {
  STATE.table = defaultTable();
  notify();
  resetRoutingFn().catch(() => {});
}

/* Build a destination-centric list of every routing slot for the routing
 * modal. Returns [{kind, index, label, channel}]. */
export function listAllDestinations() {
  const t = STATE.table ?? defaultTable();
  const rows = [];
  for (let i = 0; i < 6; ++i)
    rows.push({ kind: "fm", index: i, label: `FM Part ${i + 1}`, channel: t.fmParts[i] });
  for (let i = 0; i < 3; ++i)
    rows.push({ kind: "psg-tone", index: i, label: `PSG Tone ${i + 1}`, channel: t.psgTones[i] });
  rows.push({ kind: "psg-noise", index: 0, label: "PSG Noise", channel: t.psgNoise });
  rows.push({ kind: "dac",       index: 0, label: "DAC",       channel: t.dac });
  return rows;
}

/* Detect channels assigned to more than one destination — the conflict
 * warning in view 5. Returns Set<number>. */
export function conflictingChannels() {
  const seen = new Map();
  const dupes = new Set();
  for (const row of listAllDestinations()) {
    if (row.channel === 0) continue;   // "Off" is never a conflict
    if (seen.has(row.channel)) dupes.add(row.channel);
    else seen.set(row.channel, true);
  }
  return dupes;
}

/* -------------------------------------------------------------------------- */
/* routingStepField — inline canvas widget                                    */
/* -------------------------------------------------------------------------- */

/**
 * Mount a small step-field into `host` that edits one routing slot.
 * @param {HTMLElement} host
 * @param {{kind: string, index: number}} dest
 * @returns {{ canvas, refresh }} - the created canvas + a manual refresh hook
 */
export function routingStepField(host, dest) {
  const canvas = document.createElement("canvas");
  const innerW = readoutWidth(WIDTH_CHARS) + PAD * 2 + ARROW_W + PAD;
  const innerH = Math.max(GLYPH_H + PAD * 2, ARROW_H * 2 + 1);
  canvas.style.width  = (innerW + 2) + "px";
  canvas.style.height = (innerH + 2) + "px";
  canvas.style.cursor = "pointer";
  host.appendChild(canvas);

  const setup = setupPixelCanvas(canvas);
  const ctx = setup.ctx;
  const w = setup.width, h = setup.height;

  const grid = readoutWidth(WIDTH_CHARS);
  const ax = 1 + grid + PAD * 2;
  const upY = 1 + Math.floor((innerH - ARROW_H * 2 - 1) / 2);
  const dnY = upY + ARROW_H + 1;
  const upRect = { x: ax, y: upY, w: ARROW_W, h: ARROW_H };
  const dnRect = { x: ax, y: dnY, w: ARROW_W, h: ARROW_H };

  function text() {
    const c = getChannelForDestination(dest);
    if (c === 0) return "OFF";
    return String(c);
  }

  function render() {
    const pal = palette();
    ctx.fillStyle = pal["led-base"];
    ctx.fillRect(0, 0, w, h);
    drawBevel(ctx, 0, 0, w, h, false);
    drawLedReadout(ctx, 1 + PAD, 1 + Math.floor((h - 2 - GLYPH_H) / 2),
                   text(), WIDTH_CHARS);
    drawArrow(upRect, true);
    drawArrow(dnRect, false);
  }

  function drawArrow(rect, up) {
    const pal = palette();
    ctx.fillStyle = pal["panel"];
    ctx.fillRect(rect.x, rect.y, rect.w, rect.h);
    drawBevel(ctx, rect.x, rect.y, rect.w, rect.h, true);
    ctx.fillStyle = pal["label"];
    const cx = rect.x + Math.floor(rect.w / 2);
    if (up) {
      ctx.fillRect(cx,     rect.y + 2, 1, 1);
      ctx.fillRect(cx - 1, rect.y + 3, 3, 1);
      ctx.fillRect(cx - 2, rect.y + 4, 5, 1);
    } else {
      ctx.fillRect(cx - 2, rect.y + 2, 5, 1);
      ctx.fillRect(cx - 1, rect.y + 3, 3, 1);
      ctx.fillRect(cx,     rect.y + 4, 1, 1);
    }
  }

  canvas.addEventListener("pointerdown", (e) => {
    const rect = canvas.getBoundingClientRect();
    const x = e.clientX - rect.left;
    const y = e.clientY - rect.top;
    let cur = getChannelForDestination(dest);
    if (x >= upRect.x && x < upRect.x + upRect.w &&
        y >= upRect.y && y < upRect.y + upRect.h) {
      cur = Math.min(16, cur + 1);
    } else if (x >= dnRect.x && x < dnRect.x + dnRect.w &&
               y >= dnRect.y && y < dnRect.y + dnRect.h) {
      cur = Math.max(0, cur - 1);
    } else {
      return;
    }
    setChannelForDestination(dest, cur);
  });

  const unsub = onRoutingChange(() => render());
  fetchRouting().then(() => render());
  render();

  return {
    canvas,
    refresh: render,
    destroy: () => unsub?.(),
  };
}
