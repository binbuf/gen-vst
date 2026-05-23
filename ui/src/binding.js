/*
 * Parameter-binding helper layer (05-ui-ux.md "Parameter binding").
 *
 * Each core widget binds to its JUCE relay in one call:
 *   bindSlider("master_gain", { onChange, getDefault })
 *   bindToggle("psg_layer",   { onChange })
 *   bindCombo ("dac_rate",    { onChange })
 *
 * What you get back is a `Binding` object with the small interface every widget
 * needs: a current normalised value, a setter that pushes through to the
 * apvts, drag-start/end markers (slider only), a default-value lookup, and a
 * subscription helper that fires for every relay change. Widgets never speak to
 * `window.__JUCE__` directly — keeping the binding surface narrow makes the
 * widgets trivial to mount against scratch parameters in the gallery and the
 * fake stand-alone harness.
 *
 * The relay name equals the apvts parameter ID (05-ui-ux.md naming contract).
 */

import * as Juce from "./juce/index.js";

class SliderBinding {
  constructor(name, state) {
    this.name = name;
    this.state = state;
    this.kind = "slider";
  }

  get properties() {
    return this.state.properties;
  }

  getNormalised() {
    const v = this.state.getNormalisedValue();
    return Number.isFinite(v) ? Math.min(1, Math.max(0, v)) : 0;
  }

  getScaled() {
    return this.state.getScaledValue();
  }

  setNormalised(v) {
    const clamped = Math.min(1, Math.max(0, v));
    this.state.setNormalisedValue(clamped);
  }

  // Default value in [0,1]. JUCE's WebSliderRelay doesn't publish a default,
  // so widgets pass it in (or fall back to the parameter's start of range).
  defaultNormalised(fallback) {
    if (typeof fallback === "number") return fallback;
    return 0;
  }

  beginGesture() { this.state.sliderDragStarted(); }
  endGesture()   { this.state.sliderDragEnded(); }

  onChange(listener) {
    const id = this.state.valueChangedEvent.addListener(listener);
    return () => this.state.valueChangedEvent.removeListener(id);
  }

  onProperties(listener) {
    const id = this.state.propertiesChangedEvent.addListener(listener);
    return () => this.state.propertiesChangedEvent.removeListener(id);
  }
}

class ToggleBinding {
  constructor(name, state) {
    this.name = name;
    this.state = state;
    this.kind = "toggle";
  }

  get properties() {
    return this.state.properties;
  }

  getValue() {
    return !!this.state.getValue();
  }

  setValue(v) {
    this.state.setValue(!!v);
  }

  toggle() {
    this.setValue(!this.getValue());
  }

  onChange(listener) {
    const id = this.state.valueChangedEvent.addListener(listener);
    return () => this.state.valueChangedEvent.removeListener(id);
  }

  onProperties(listener) {
    const id = this.state.propertiesChangedEvent.addListener(listener);
    return () => this.state.propertiesChangedEvent.removeListener(id);
  }
}

class ComboBinding {
  constructor(name, state) {
    this.name = name;
    this.state = state;
    this.kind = "combo";
  }

  get properties() {
    return this.state.properties;
  }

  getChoices() {
    return this.state.properties.choices ?? [];
  }

  getIndex() {
    return this.state.getChoiceIndex();
  }

  setIndex(i) {
    const n = this.getChoices().length;
    if (n === 0) return;
    const clamped = Math.max(0, Math.min(n - 1, i | 0));
    this.state.setChoiceIndex(clamped);
  }

  onChange(listener) {
    const id = this.state.valueChangedEvent.addListener(listener);
    return () => this.state.valueChangedEvent.removeListener(id);
  }

  onProperties(listener) {
    const id = this.state.propertiesChangedEvent.addListener(listener);
    return () => this.state.propertiesChangedEvent.removeListener(id);
  }
}

export function bindSlider(name) {
  return new SliderBinding(name, Juce.getSliderState(name));
}

export function bindToggle(name) {
  return new ToggleBinding(name, Juce.getToggleState(name));
}

export function bindCombo(name) {
  return new ComboBinding(name, Juce.getComboBoxState(name));
}

// Convenience for a widget that only needs a one-shot, current-value read at
// mount time — most widgets repaint via onChange instead.
export function readNormalised(binding) {
  return binding.getNormalised();
}
