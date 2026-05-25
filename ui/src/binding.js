// Parameter-binding helpers — thin wrappers around the JUCE 8
// SliderState / ToggleState / ComboBoxState objects exposed on
// `window.__JUCE__` (see ui/src/juce/index.js).
//
// Every widget binds to its relay through one of these calls. The relay name
// equals the apvts parameter ID (05-ui-ux.md "Parameter binding"). Each
// helper returns a small controller object with the operations the widget
// actually needs:
//
//   bindSlider(name)  -> { getNormalised, setNormalised, beginGesture,
//                          endGesture, onChange, defaultNormalised }
//   bindToggle(name)  -> { get, set, onChange }
//   bindCombo(name)   -> { getIndex, setIndex, onChange, choices }
//
// The controllers swallow the underlying JUCE listener lifetimes — callers
// don't need to manage subscription IDs unless they explicitly want to
// dispose. `dispose()` is exposed for that case (used by the gallery so
// hot-reload doesn't leak listeners).

import {
  getSliderState,
  getToggleState,
  getComboBoxState,
} from "./juce/index.js";

const noop = () => {};

// ------------------------------------------------------------------ slider
// A "slider" in JUCE-land covers every continuous parameter — knobs,
// horizontal sliders, decimator-knob, stepper. The relay name is the apvts
// param ID; properties.start / .end / .skew describe the parameter's range.
export function bindSlider(name, opts = {}) {
  const state = getSliderState(name);

  const getNormalised = () => state.getNormalisedValue();
  const setNormalised = (v) => {
    // Defensive clamp — widgets shouldn't push out-of-range values, but a
    // stray drag past the rail shouldn't poison the relay.
    const clamped = Math.max(0, Math.min(1, v));
    state.setNormalisedValue(clamped);
  };
  const beginGesture = () => state.sliderDragStarted();
  const endGesture = () => state.sliderDragEnded();

  // Listener returns a remover; widgets unsubscribe via the controller's
  // dispose() rather than tracking IDs themselves.
  const listeners = new Set();
  const subId = state.valueChangedEvent.addListener(() => {
    for (const fn of listeners) fn(state.getNormalisedValue());
  });

  const onChange = (cb) => {
    listeners.add(cb);
    // Fire once on subscribe so the widget hydrates to the current value
    // without needing a separate initial-paint code path.
    cb(state.getNormalisedValue());
    return () => listeners.delete(cb);
  };

  const defaultNormalised = (fallback = 0.5) => {
    // JUCE doesn't expose the parameter's default through SliderState, so
    // we accept a per-widget fallback. Widgets should pass the actual apvts
    // default; the gallery uses 0.5 across the board.
    return fallback;
  };

  const dispose = () => {
    state.valueChangedEvent.removeListener(subId);
    listeners.clear();
  };

  return {
    name,
    getNormalised,
    setNormalised,
    beginGesture,
    endGesture,
    onChange,
    defaultNormalised,
    dispose,
    // The raw underlying state — escape hatch for widgets that need
    // properties.start / properties.end (e.g. stepper for integer scaling).
    state,
  };
}

// ------------------------------------------------------------------ toggle
export function bindToggle(name) {
  const state = getToggleState(name);

  const get = () => state.getValue();
  const set = (b) => state.setValue(Boolean(b));

  const listeners = new Set();
  const subId = state.valueChangedEvent.addListener(() => {
    for (const fn of listeners) fn(state.getValue());
  });
  const onChange = (cb) => {
    listeners.add(cb);
    cb(state.getValue());
    return () => listeners.delete(cb);
  };

  const dispose = () => {
    state.valueChangedEvent.removeListener(subId);
    listeners.clear();
  };

  return { name, get, set, onChange, dispose, state };
}

// ------------------------------------------------------------------ combo
export function bindCombo(name) {
  const state = getComboBoxState(name);

  const getIndex = () => state.getChoiceIndex();
  const setIndex = (i) => state.setChoiceIndex(i);

  const listeners = new Set();
  const subId = state.valueChangedEvent.addListener(() => {
    for (const fn of listeners) fn(state.getChoiceIndex());
  });
  const onChange = (cb) => {
    listeners.add(cb);
    cb(state.getChoiceIndex());
    return () => listeners.delete(cb);
  };

  const choices = () => state.properties.choices || [];

  const dispose = () => {
    state.valueChangedEvent.removeListener(subId);
    listeners.clear();
  };

  return { name, getIndex, setIndex, onChange, choices, dispose, state };
}

// --- Backend event subscriptions ---------------------------------------
// Thin pass-through over window.__JUCE__.backend.addEventListener so widget
// modules don't reach across to the JUCE shim directly. Used by
// notification-toast (`notify` events) and level-meter (`meterData` events).
export function onBackendEvent(eventId, handler) {
  const id = window.__JUCE__.backend.addEventListener(eventId, handler);
  return () => window.__JUCE__.backend.removeEventListener(id);
}

// Emit one of the C++ side's listener events. Used by the gallery to push a
// synthetic `notify` and meterData payload without needing a real audio path.
export function emitBackendEvent(eventId, payload) {
  window.__JUCE__.backend.emitByBackend(eventId, JSON.stringify(payload));
}

// no-op export so tree-shakers leave the module structure intact.
export const __noop = noop;
