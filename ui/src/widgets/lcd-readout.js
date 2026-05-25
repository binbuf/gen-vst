// `lcd-readout` widget — canvas-only flat-dark readout with glowing text.
//
// Recipe in 09-visual-spec.md *LCD readout (canvas)*: deep navy background
// + top inner-shadow gradient + two-pass glowing text (blurred bloom then
// sharp).
//
// Sized by opts.width / opts.height (defaults below). Renders any string
// passed to setText(...) or any number passed to setValue(...). The widget
// is display-only — no input, no apvts binding (host wires that).

import { applyTooltip } from "./tooltip-content.js";

function cssVar(name, fallback = "#000") {
  const v = getComputedStyle(document.documentElement)
    .getPropertyValue(name).trim();
  return v || fallback;
}

export function mount(host, opts = {}) {
  const {
    width = 48,
    height = 20,
    fontPx = 10,
    align = "center",        // "left" | "center" | "right"
    initialText = "—",
    tipId = null,
  } = opts;

  host.classList.add("lcd-readout-host");
  host.style.position = "relative";
  host.style.display = "inline-block";
  host.style.width  = `${width}px`;
  host.style.height = `${height}px`;
  if (tipId) applyTooltip(host, tipId);

  const canvas = document.createElement("canvas");
  canvas.style.width  = `${width}px`;
  canvas.style.height = `${height}px`;
  canvas.style.display = "block";
  host.appendChild(canvas);

  const ctx = canvas.getContext("2d");
  const dpr = window.devicePixelRatio || 1;
  canvas.width  = Math.round(width * dpr);
  canvas.height = Math.round(height * dpr);
  ctx.scale(dpr, dpr);
  ctx.imageSmoothingEnabled = true;

  let textValue = initialText;

  const draw = () => {
    const w = width, h = height;
    ctx.clearRect(0, 0, w, h);

    // 1. base + recessed inset
    ctx.fillStyle = cssVar("--lcd-bg", "#0d1424");
    ctx.fillRect(0, 0, w, h);

    const innerShadow = ctx.createLinearGradient(0, 0, 0, h);
    innerShadow.addColorStop(0, "rgba(0,0,0,0.55)");
    innerShadow.addColorStop(0.18, "transparent");
    innerShadow.addColorStop(0.82, "transparent");
    innerShadow.addColorStop(1, "rgba(78,160,255,0.04)");
    ctx.fillStyle = innerShadow;
    ctx.fillRect(0, 0, w, h);

    // 1px LCD bezel (so the readout reads as an inset even when nested in
    // a chassis-coloured cell rather than .lcd)
    ctx.strokeStyle = "rgba(0,0,0,0.7)";
    ctx.lineWidth = 1;
    ctx.strokeRect(0.5, 0.5, w - 1, h - 1);

    // 2. text — two-pass (bloom, then sharp)
    const text = String(textValue);
    ctx.font = `500 ${fontPx}px "IBM Plex Mono", monospace`;
    ctx.textBaseline = "middle";
    const padX = 4;
    let tx;
    if      (align === "left")  { ctx.textAlign = "left";   tx = padX;        }
    else if (align === "right") { ctx.textAlign = "right";  tx = w - padX;    }
    else                         { ctx.textAlign = "center"; tx = w / 2;       }
    const ty = h / 2 + 1;

    ctx.shadowColor = cssVar("--lcd-text-glow", "rgba(78,160,255,0.6)");
    ctx.shadowBlur  = fontPx * 0.7;
    ctx.fillStyle   = cssVar("--lcd-text-on", "#4ea0ff");
    ctx.fillText(text, tx, ty);

    ctx.shadowBlur = 0;
    ctx.fillText(text, tx, ty);
  };

  draw();

  return {
    setText(s) { textValue = s; draw(); },
    setValue(n, fixed = 0) {
      const num = (typeof n === "number" && Number.isFinite(n))
        ? n.toFixed(fixed)
        : "—";
      textValue = num;
      draw();
    },
    redraw: draw,
    dispose() {
      if (canvas.parentNode === host) host.removeChild(canvas);
    },
  };
}
