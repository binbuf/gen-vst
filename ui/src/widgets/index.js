/*
 * Core widget library — Task 10, extended in Task 11.
 *
 * Re-exports every reusable widget plus the shared pixel-art helpers and the
 * binding helper layer, so consumers (the main page, the gallery, and the
 * view-specific widgets in Task 11) have one import surface.
 */

// Task 10 core widgets
export { Knob } from "./knob.js";
export { Slider } from "./slider.js";
export { LedReadout } from "./led-readout.js";
export { StepField } from "./step-field.js";
export { Toggle } from "./toggle.js";
export { SectionTabs } from "./section-tabs.js";
export { LcdList } from "./lcd-list.js";

// Task 11 FM-specific widgets
export { SegDisplay } from "./seg-display.js";
export { AlgoButtons } from "./algo-buttons.js";
export { AlgoDiagram } from "./algo-diagram.js";
export { AdsrGraph } from "./adsr-graph.js";
export { OperatorPanel } from "./operator-panel.js";
export { Wordmark } from "./wordmark.js";
export { GearIcon } from "./gear-icon.js";
export { Oscilloscope } from "./oscilloscope.js";
export { VuMeter } from "./vu-meter.js";
export { VoiceLeds } from "./voice-leds.js";
export { ClipLed } from "./clip-led.js";

// Task 13 widgets
export { WaveformDisplay } from "./waveform-display.js";
export { NotificationToastHost } from "./notification-toast.js";

export * from "./pixel.js";
export * from "../binding.js";
