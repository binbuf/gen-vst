/*
 * Master-gain knob — Canvas-drawn, two-way bound to a JUCE WebSliderRelay.
 *
 * A deliberately simple knob for the WebView shell (Task 03): it proves the
 * full C++<->JS parameter contract with one control. The pixel-perfect Canvas
 * widget library (knob, slider, led-readout, ...) lands in Task 10.
 *
 * Interaction (genny-ui.md): ~270 deg sweep, rest at 7 o'clock, vertical
 * click-drag (up = increase), Shift = fine, double-click = reset.
 */

const START_ANGLE_DEG = 120; // 7 o'clock rest position
const SWEEP_DEG = 270;
const DRAG_RANGE_PX = 200; // vertical travel for a full 0..1 sweep
const FINE_FACTOR = 0.2; // Shift-drag multiplier

function cssVar(name) {
  return getComputedStyle(document.documentElement)
    .getPropertyValue(name)
    .trim();
}

function clamp01(x) {
  return Math.min(1, Math.max(0, x));
}

export class Knob {
  constructor(canvas, sliderState, options = {}) {
    this.canvas = canvas;
    this.ctx = canvas.getContext("2d");
    this.ctx.imageSmoothingEnabled = false;
    this.state = sliderState;
    this.resetValue = options.resetValue ?? 0;
    this.onChange = options.onChange ?? (() => {});

    this.colors = {
      ring: cssVar("--knob-ring"),
      body: cssVar("--knob-body"),
      dot: cssVar("--knob-dot"),
    };

    this.dragging = false;
    this.dragStartY = 0;
    this.dragStartValue = 0;

    // Repaint on every parameter change — a UI drag, a host automation move,
    // or the initial sync (05-ui-ux.md "Parameter binding", two-way).
    this.state.valueChangedEvent.addListener(() => this.render());

    canvas.addEventListener("pointerdown", (e) => this.onPointerDown(e));
    canvas.addEventListener("pointermove", (e) => this.onPointerMove(e));
    canvas.addEventListener("pointerup", (e) => this.onPointerUp(e));
    canvas.addEventListener("pointercancel", (e) => this.onPointerUp(e));
    canvas.addEventListener("dblclick", (e) => this.onDoubleClick(e));

    this.render();
  }

  value() {
    return clamp01(this.state.getNormalisedValue());
  }

  onPointerDown(e) {
    this.dragging = true;
    this.dragStartY = e.clientY;
    this.dragStartValue = this.value();
    this.state.sliderDragStarted();
    this.canvas.setPointerCapture(e.pointerId);
    e.preventDefault();
  }

  onPointerMove(e) {
    if (!this.dragging) return;
    const dy = this.dragStartY - e.clientY; // drag up = increase
    const scale = e.shiftKey ? FINE_FACTOR : 1;
    const next = this.dragStartValue + (dy / DRAG_RANGE_PX) * scale;
    this.state.setNormalisedValue(clamp01(next));
  }

  onPointerUp(e) {
    if (!this.dragging) return;
    this.dragging = false;
    this.state.sliderDragEnded();
    if (this.canvas.hasPointerCapture(e.pointerId))
      this.canvas.releasePointerCapture(e.pointerId);
  }

  onDoubleClick(e) {
    e.preventDefault();
    this.state.sliderDragStarted();
    this.state.setNormalisedValue(this.resetValue);
    this.state.sliderDragEnded();
  }

  render() {
    const v = this.value();
    this.onChange(v);
    this.draw(v);
  }

  draw(v) {
    const ctx = this.ctx;
    const { width: w, height: h } = this.canvas;
    const cx = w / 2;
    const cy = h / 2;
    const r = Math.min(cx, cy) - 2;

    ctx.clearRect(0, 0, w, h);

    // Outer ring
    ctx.fillStyle = this.colors.ring;
    ctx.beginPath();
    ctx.arc(cx, cy, r, 0, Math.PI * 2);
    ctx.fill();

    // Body
    ctx.fillStyle = this.colors.body;
    ctx.beginPath();
    ctx.arc(cx, cy, r - 5, 0, Math.PI * 2);
    ctx.fill();

    // Indicator — a notch from the body toward the current value angle.
    const angle = ((START_ANGLE_DEG + v * SWEEP_DEG) * Math.PI) / 180;
    const dx = Math.cos(angle);
    const dy = Math.sin(angle);

    ctx.strokeStyle = this.colors.dot;
    ctx.lineWidth = 4;
    ctx.lineCap = "butt";
    ctx.beginPath();
    ctx.moveTo(cx + dx * r * 0.3, cy + dy * r * 0.3);
    ctx.lineTo(cx + dx * r * 0.78, cy + dy * r * 0.78);
    ctx.stroke();

    // Square indicator dot at the rim end of the notch.
    const dot = 4;
    ctx.fillStyle = this.colors.dot;
    ctx.fillRect(
      Math.round(cx + dx * r * 0.78 - dot / 2),
      Math.round(cy + dy * r * 0.78 - dot / 2),
      dot,
      dot,
    );
  }
}
