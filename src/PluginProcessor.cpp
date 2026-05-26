#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <string_view>
#include <system_error>
#include <vector>

#include "FmRegisterMap.h"
#include "PluginState.h"
#include "PsgPreset.h"

namespace
{
    // Resolve the factory-root path at runtime (ADR-0005). Standalone uses the
    // platform data directory the install rule writes to; plugin formats walk
    // upward from the loaded binary looking for Resources/patches.
    std::filesystem::path resolveFactoryRoot (juce::AudioProcessor::WrapperType wt)
    {
        if (wt == juce::AudioProcessor::wrapperType_Standalone)
        {
           #ifdef GENVST_STANDALONE_PATCH_DIR
            return std::filesystem::path { GENVST_STANDALONE_PATCH_DIR };
           #else
            return {};
           #endif
        }

        const auto exec = juce::File::getSpecialLocation (
                              juce::File::SpecialLocationType::currentExecutableFile);

        juce::File cur = exec.getParentDirectory();
        for (int i = 0; i < 6 && cur.exists(); ++i)
        {
            const auto candidate1 = cur.getChildFile ("Resources").getChildFile ("patches");
            if (candidate1.isDirectory())
                return std::filesystem::path (candidate1.getFullPathName().toRawUTF8());

            const auto candidate2 = cur.getChildFile ("Contents").getChildFile ("Resources").getChildFile ("patches");
            if (candidate2.isDirectory())
                return std::filesystem::path (candidate2.getFullPathName().toRawUTF8());

            cur = cur.getParentDirectory();
        }

       #ifdef GENVST_DEV_PATCH_DIR
        {
            const std::filesystem::path devDir { GENVST_DEV_PATCH_DIR };
            std::error_code ec;
            if (std::filesystem::is_directory (devDir, ec))
                return devDir;
        }
       #endif

        return {};
    }

    // Soft-clip guard.
    inline float softClip (float x) noexcept
    {
        constexpr float threshold = 0.9f;
        constexpr float headroom  = 1.0f - threshold;
        if (x >  threshold) return  threshold + headroom * std::tanh ((x - threshold) / headroom);
        if (x < -threshold) return -threshold + headroom * std::tanh ((x + threshold) / headroom);
        return x;
    }

    // --- FM parameter schema --------------------------------------------------
    // The per-channel FM parameter set is generated from these two tables —
    // the v1 _part<n> suffix is gone (single-engine; one patch's worth of
    // params). Each row maps a parameter to the Patch field it mirrors and
    // its hardware range.

    struct OpParamDesc
    {
        const char* id;                       // "<id>_op<1-4>"
        std::uint8_t (Patch::* field)[4];
        int lo, hi;
    };

    struct PartParamDesc
    {
        const char* id;                       // "<id>"
        std::uint8_t Patch::* field;
        int lo, hi;
    };

    constexpr OpParamDesc kOpParams[]
    {
        { "dt",   &Patch::dt,   0, 6   },
        { "mul",  &Patch::mul,  0, 15  },
        { "tl",   &Patch::tl,   0, 127 },
        { "ks",   &Patch::ks,   0, 3   },
        { "ar",   &Patch::ar,   0, 31  },
        { "dr",   &Patch::dr,   0, 31  },
        { "sr",   &Patch::sr,   0, 31  },
        { "rr",   &Patch::rr,   0, 15  },
        { "sl",   &Patch::sl,   0, 15  },
        { "ssg",  &Patch::ssg,  0, 15  },
        { "amon", &Patch::amon, 0, 1   },
    };

    constexpr PartParamDesc kPartParams[]
    {
        { "alg",        &Patch::alg,        0, 7 },
        { "fb",         &Patch::fb,         0, 7 },
        { "ams",        &Patch::ams,        0, 3 },
        { "pms",        &Patch::pms,        0, 7 },
        { "lr",         &Patch::lr,         0, 3 },
        { "lfo_enable", &Patch::lfo_enable, 0, 1 },
        { "lfo_rate",   &Patch::lfo_rate,   0, 7 },
    };

    static_assert (sizeof (kOpParams)   / sizeof (kOpParams[0])   == FmParamCache::kNumOpParams);
    static_assert (sizeof (kPartParams) / sizeof (kPartParams[0]) == FmParamCache::kNumPartParams);

    juce::String opParamId (const char* id, int op)
    {
        return juce::String (id) + "_op" + juce::String (op + 1);
    }

    juce::String displayName (const char* id, int op)
    {
        juce::String name = juce::String (id).toUpperCase().replace ("_", " ");
        if (op >= 0)
            name += " Op" + juce::String (op + 1);
        return name;
    }

    constexpr int kPsgChannels = SN76489Engine::kNumChannels;     // 3 tones + 1 noise
    constexpr int kPsgTones    = SN76489Engine::kNumToneChs;      // 3

    // 14-bit signed pitch wheel (0..16383, 8192=centre) -> semitone offset.
    double pitchBendToSemitones (int bend14bit, int rangeSemitones) noexcept
    {
        const double centred = static_cast<double> (bend14bit - 8192) / 8192.0;
        return centred * static_cast<double> (rangeSemitones);
    }

    // -1..+1 normalised pitch-bend mirror (for the apvts display-only param).
    float pitchBendNormalised (int bend14bit) noexcept
    {
        const double centred = static_cast<double> (bend14bit - 8192) / 8192.0;
        return static_cast<float> (juce::jlimit (-1.0, 1.0, centred));
    }

    // Carrier-operator bitmask per YM2612 algorithm (02-fm-synthesis.md
    // *FM Algorithms* Carriers column). Bit i = operator i+1 is a carrier.
    // ALG 0..3 → S4; ALG 4 → S2, S4; ALG 5,6 → S2, S3, S4; ALG 7 → all four.
    constexpr std::uint8_t kCarrierMaskForAlg[8] = {
        0b1000,   // 0: S4
        0b1000,   // 1: S4
        0b1000,   // 2: S4
        0b1000,   // 3: S4
        0b1010,   // 4: S2 + S4
        0b1110,   // 5: S2 + S3 + S4
        0b1110,   // 6: S2 + S3 + S4
        0b1111,   // 7: S1 + S2 + S3 + S4
    };
}

juce::AudioProcessorValueTreeState::ParameterLayout GenVstAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // --- Global / mode -------------------------------------------------------
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "mode_select", 1 },
        "Mode",
        juce::StringArray { "FM", "SQ", "D" }, 0));
    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "output_filter", 1 }, "Output Filter", true));
    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "ladder_effect", 1 }, "Ladder Effect", true));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "master_volume", 1 }, "Master Volume",
        juce::NormalisableRange<float> (0.0f, 1.0f), 0.8f));

    // --- FM (single patch) ---------------------------------------------------
    // 1-bit register fields (lfo_enable, amon) ship as AudioParameterBool so the
    // editor's per-parameter relay dispatch (PluginEditor.cpp dynamic_cast chain)
    // picks WebToggleButtonRelay — matching the JS-side bindToggle("lfo_enable")
    // / bindToggle("amon_op*") calls. Declaring them as AudioParameterInt(0,1)
    // routes through WebSliderRelay, so the toggle events go to a phantom relay
    // and UI clicks for LFO on/off and per-op AM-ON never reach audio.
    for (const auto& d : kPartParams)
    {
        if (std::string_view (d.id) == "lfo_enable")
            layout.add (std::make_unique<juce::AudioParameterBool> (
                juce::ParameterID { d.id, 1 }, displayName (d.id, -1), false));
        else
            layout.add (std::make_unique<juce::AudioParameterInt> (
                juce::ParameterID { d.id, 1 }, displayName (d.id, -1),
                d.lo, d.hi, d.lo));
    }

    for (const auto& d : kOpParams)
        for (int op = 0; op < FmParamCache::kNumOps; ++op)
        {
            if (std::string_view (d.id) == "amon")
            {
                layout.add (std::make_unique<juce::AudioParameterBool> (
                    juce::ParameterID { opParamId (d.id, op), 1 },
                    displayName (d.id, op), false));
                continue;
            }
            // TL / SL apvts surface is *level* (0 = silent, max = loud) per
            // 02-fm-synthesis.md *UI level vs hardware attenuation*. Default
            // each to its max so a fresh apvts is audible — the v1 default of
            // 0 = loudest-attenuation translates to v2 max = loudest-level.
            const int defaultValue = (std::string_view (d.id) == "tl"
                                       || std::string_view (d.id) == "sl")
                                       ? d.hi : d.lo;
            layout.add (std::make_unique<juce::AudioParameterInt> (
                juce::ParameterID { opParamId (d.id, op), 1 },
                displayName (d.id, op),
                d.lo, d.hi, defaultValue));
        }

    // FM v2 additions — 04-patch-system.md *Defaults on legacy-format load*,
    // 02-fm-synthesis.md *FREQ Control Mode*. The third mode is `AUTO_RETRIG`
    // (CSM + TimerA), not `FIXED_HZ` — `fixed[op]` is a separate per-op toggle
    // that's effective in FLOAT_MUL and AUTO_RETRIG.
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "freq_ctrl_mode", 1 }, "Freq Ctrl Mode",
        juce::StringArray { "INT_MUL", "FLOAT_MUL", "AUTO_RETRIG" }, 0));
    layout.add (std::make_unique<juce::AudioParameterInt> (
        juce::ParameterID { "retrig_rate", 1 }, "Retrig Rate", 0, 1023, 498));

    for (int op = 0; op < FmParamCache::kNumOps; ++op)
    {
        const juce::String suffix = "_op" + juce::String (op + 1);
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { "mul_float" + suffix, 1 }, "Mul Float Op" + juce::String (op + 1),
            juce::NormalisableRange<float> (0.5f, 15.99f), 1.0f));
        layout.add (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID { "fixed" + suffix, 1 }, "Fixed Op" + juce::String (op + 1),
            false));
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { "freq_fixed_hz" + suffix, 1 }, "Fixed Hz Op" + juce::String (op + 1),
            juce::NormalisableRange<float> (20.0f, 20000.0f), 440.0f));
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { "vel" + suffix, 1 }, "Vel Op" + juce::String (op + 1),
            juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f));
    }

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "channel_tl", 1 }, "Channel TL",
        juce::NormalisableRange<float> (0.0f, 1.0f), 1.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "fm_dac_prescaler", 1 }, "FM DAC Prescaler",
        juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "mod_wheel_value", 1 }, "Mod Wheel",
        juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "pitch_bend_value", 1 }, "Pitch Bend",
        juce::NormalisableRange<float> (-1.0f, 1.0f), 0.0f));
    // Bool, not Choice — the UI binds it via bindToggle (two-way toggle),
    // and JUCE 8's WebView relays are namespaced by type: a Choice param
    // would need a WebComboBoxRelay that the toggle widget can't connect
    // to. The audio side reads `noteModeParam->load() > 0.5f`, which
    // behaves identically for Bool (false=RETRIG, true=LEGATO).
    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "note_mode", 1 }, "Note Mode", false));
    layout.add (std::make_unique<juce::AudioParameterInt> (
        juce::ParameterID { "poly_voices", 1 }, "Poly Voices", 1, 16, 16));
    layout.add (std::make_unique<juce::AudioParameterInt> (
        juce::ParameterID { "pitch_bend_range", 1 }, "Pitch Bend Range", 1, 12, 2));
    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "hardware_strict", 1 }, "Hardware Strict", false));

    // --- SQ ------------------------------------------------------------------
    static const std::array<const char*, kPsgChannels> kPsgIds { "ch1", "ch2", "ch3", "noise" };

    for (int i = 0; i < kPsgChannels; ++i)
    {
        const auto suffix = juce::String ("_") + kPsgIds[(std::size_t) i];
        const auto label  = juce::String ("PSG ") + kPsgIds[(std::size_t) i];

        layout.add (std::make_unique<juce::AudioParameterInt> (
            juce::ParameterID { "psg_atk" + suffix, 1 }, label + " ATK", 0, 31, 0));
        layout.add (std::make_unique<juce::AudioParameterInt> (
            juce::ParameterID { "psg_dr1" + suffix, 1 }, label + " DR1", 0, 31, 0));
        layout.add (std::make_unique<juce::AudioParameterInt> (
            juce::ParameterID { "psg_sus" + suffix, 1 }, label + " SUS", 0, 15, 0));
        layout.add (std::make_unique<juce::AudioParameterInt> (
            juce::ParameterID { "psg_dr2" + suffix, 1 }, label + " DR2", 0, 31, 0));
        layout.add (std::make_unique<juce::AudioParameterInt> (
            juce::ParameterID { "psg_rr" + suffix, 1 },  label + " RR",  0, 15, 0));
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { "psg_vel" + suffix, 1 }, label + " VEL",
            juce::NormalisableRange<float> (0.0f, 1.0f), 1.0f));
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { "psg_vol" + suffix, 1 }, label + " VOL",
            juce::NormalisableRange<float> (0.0f, 1.0f), 1.0f));
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { "psg_pan" + suffix, 1 }, label + " PAN",
            juce::NormalisableRange<float> (-1.0f, 1.0f), 0.0f));
    }

    // Detune + glide live on tone channels only (no pitch on noise).
    for (int i = 0; i < kPsgTones; ++i)
    {
        const auto suffix = juce::String ("_") + kPsgIds[(std::size_t) i];
        const auto label  = juce::String ("PSG ") + kPsgIds[(std::size_t) i];

        layout.add (std::make_unique<juce::AudioParameterInt> (
            juce::ParameterID { "psg_detune" + suffix, 1 }, label + " DETUNE", -100, 100, 0));
        layout.add (std::make_unique<juce::AudioParameterInt> (
            juce::ParameterID { "psg_glide" + suffix, 1 }, label + " GLIDE", 0, 2000, 0));
    }

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "psg_noise_type", 1 }, "PSG Noise Type",
        juce::StringArray { "white", "periodic" }, 0));
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "psg_noise_rate", 1 }, "PSG Noise Rate",
        juce::StringArray { "low", "mid", "high", "ch2" }, 1));
    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "psg_noise_auto", 1 }, "PSG Noise Auto", false));

    // --- D -------------------------------------------------------------------
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "prescaler", 1 }, "Prescaler",
        juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "mono", 1 }, "Mono", false));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "dry_wet", 1 }, "Dry/Wet",
        juce::NormalisableRange<float> (0.0f, 1.0f), 1.0f));

    // --- Settings-bound globals ---------------------------------------------
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "aftertouch_target", 1 }, "Aftertouch Target",
        juce::StringArray { "Off", "LFO", "TL" }, 1));
    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "velocity_to_tl", 1 }, "Velocity -> TL", true));
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "ui_scale", 1 }, "UI Scale",
        juce::StringArray { "1x", "2x", "3x" }, 0));
    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "tooltips_enabled", 1 }, "Tooltips", true));

    // --- Gallery scratch params (Task 04 widget gallery) --------------------
    // Bound by ui/gallery.html so every widget kind can be developed and
    // verified against a live apvts parameter. Storage cost is trivial; left
    // unguarded so the host's generic editor can drive them too. Visible as
    // "GALLERY ..." in the host parameter list — irrelevant for end users.
    for (char id : { 'a', 'b', 'c', 'd' })
    {
        const juce::String suffix = juce::String::charToString (id);
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { "gallery_knob_" + suffix, 1 },
            "GALLERY Knob " + suffix.toUpperCase(),
            juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f));
        layout.add (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID { "gallery_toggle_" + suffix, 1 },
            "GALLERY Toggle " + suffix.toUpperCase(),
            false));
    }
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "gallery_combo_a", 1 }, "GALLERY Combo A",
        juce::StringArray { "Alpha", "Beta", "Gamma", "Delta" }, 0));
    // Algorithm picker scratch — 0..7 selects an algo-grid button.
    layout.add (std::make_unique<juce::AudioParameterInt> (
        juce::ParameterID { "gallery_algo", 1 }, "GALLERY Algorithm",
        0, 7, 0));
    // Stepper scratch — wide integer range so click-and-hold repeat is
    // visible.
    layout.add (std::make_unique<juce::AudioParameterInt> (
        juce::ParameterID { "gallery_stepper", 1 }, "GALLERY Stepper",
        0, 999, 0));
    // Level-meter scratch — slider-driven amplitude so the gallery can
    // exercise the meter without needing live audio.
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "gallery_level", 1 }, "GALLERY Level",
        juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f));
    // Note-on LED scratch — bound to a toggle so the gallery can light the
    // LED without needing a key-on event.
    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "gallery_noteon", 1 }, "GALLERY Note On",
        false));
    // MIDI-wheel scratch — one shared float, the PB widget reads it ±1
    // (mapping to 0..1) and the MW widget reads it 0..1 directly so a
    // single drag exercises both variants.
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "gallery_wheel", 1 }, "GALLERY Wheel",
        juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f));

    return layout;
}

void FmParamCache::connect (juce::AudioProcessorValueTreeState& apvts)
{
    for (int d = 0; d < kNumOpParams; ++d)
        for (int op = 0; op < kNumOps; ++op)
            opParam[d][op] = apvts.getRawParameterValue (opParamId (kOpParams[d].id, op));

    for (int d = 0; d < kNumPartParams; ++d)
        partParam[d] = apvts.getRawParameterValue (juce::String (kPartParams[d].id));

    for (int op = 0; op < kNumOps; ++op)
    {
        const juce::String suffix = "_op" + juce::String (op + 1);
        mulFloatParam   [op] = apvts.getRawParameterValue ("mul_float"     + suffix);
        fixedParam      [op] = apvts.getRawParameterValue ("fixed"         + suffix);
        freqFixedHzParam[op] = apvts.getRawParameterValue ("freq_fixed_hz" + suffix);
        velParam        [op] = apvts.getRawParameterValue ("vel"           + suffix);
    }
    channelTlParam      = apvts.getRawParameterValue ("channel_tl");
    fmDacPrescalerParam = apvts.getRawParameterValue ("fm_dac_prescaler");
    freqCtrlModeParam   = apvts.getRawParameterValue ("freq_ctrl_mode");
    retrigRateParam     = apvts.getRawParameterValue ("retrig_rate");
}

void FmParamCache::readPatch (Patch& dest) const noexcept
{
    for (int d = 0; d < kNumOpParams; ++d)
        for (int op = 0; op < kNumOps; ++op)
            (dest.*(kOpParams[d].field))[op] =
                static_cast<std::uint8_t> (juce::roundToInt (opParam[d][op]->load()));

    for (int d = 0; d < kNumPartParams; ++d)
        dest.*(kPartParams[d].field) =
            static_cast<std::uint8_t> (juce::roundToInt (partParam[d]->load()));

    // TL / SL apvts surface is *level* (0 = silent, max = loudest); the Patch
    // stores hardware *attenuation*. Invert here on the way out so the Voice's
    // register-write path sees attenuation as it always has
    // (02-fm-synthesis.md *UI level vs hardware attenuation*).
    for (int op = 0; op < kNumOps; ++op)
    {
        dest.tl[op] = static_cast<std::uint8_t> (
            FmRegisterMap::levelToAttenuation (dest.tl[op], 127));
        dest.sl[op] = static_cast<std::uint8_t> (
            FmRegisterMap::levelToAttenuation (dest.sl[op], 15));
    }

    // v2 fields — no inversion needed, all direct float / bool / enum reads.
    for (int op = 0; op < kNumOps; ++op)
    {
        dest.mul_float    [op] = mulFloatParam    [op] != nullptr
                                    ? mulFloatParam    [op]->load() : 1.0f;
        dest.fixed        [op] = fixedParam       [op] != nullptr
                                    ? fixedParam       [op]->load() > 0.5f : false;
        dest.freq_fixed_hz[op] = freqFixedHzParam [op] != nullptr
                                    ? freqFixedHzParam [op]->load() : 440.0f;
        dest.vel          [op] = velParam         [op] != nullptr
                                    ? velParam         [op]->load() : 0.0f;
    }
    dest.channel_tl       = channelTlParam      != nullptr ? channelTlParam     ->load() : 1.0f;
    dest.fm_dac_prescaler = fmDacPrescalerParam != nullptr ? fmDacPrescalerParam->load() : 0.0f;
    dest.freq_ctrl_mode   = freqCtrlModeParam   != nullptr
                              ? static_cast<std::uint8_t> (
                                    juce::jlimit (0, 2, juce::roundToInt (freqCtrlModeParam->load())))
                              : 0;
    dest.retrig_rate      = retrigRateParam     != nullptr
                              ? static_cast<std::uint16_t> (
                                    juce::jlimit (0, 1023, juce::roundToInt (retrigRateParam->load())))
                              : 500;
}

GenVstAudioProcessor::GenVstAudioProcessor()
    : juce::AudioProcessor (BusesProperties()
        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    modeSelectParam      = apvts.getRawParameterValue ("mode_select");
    masterVolumeParam    = apvts.getRawParameterValue ("master_volume");
    outputFilterParam    = apvts.getRawParameterValue ("output_filter");
    ladderEffectParam    = apvts.getRawParameterValue ("ladder_effect");
    pitchBendRangeParam  = apvts.getRawParameterValue ("pitch_bend_range");
    modWheelMirrorParam  = apvts.getRawParameterValue ("mod_wheel_value");
    pitchBendMirrorParam = apvts.getRawParameterValue ("pitch_bend_value");
    prescalerParam       = apvts.getRawParameterValue ("prescaler");
    monoParam            = apvts.getRawParameterValue ("mono");
    dryWetParam          = apvts.getRawParameterValue ("dry_wet");
    noteModeParam        = apvts.getRawParameterValue ("note_mode");
    polyVoicesParam      = apvts.getRawParameterValue ("poly_voices");
    velocityToTlParam    = apvts.getRawParameterValue ("velocity_to_tl");
    hardwareStrictParam  = apvts.getRawParameterValue ("hardware_strict");
    aftertouchTargetParam = apvts.getRawParameterValue ("aftertouch_target");

    // SQ engine param cache. Mirrors the suffix ordering in
    // createParameterLayout() — ch1 / ch2 / ch3 / noise.
    static constexpr const char* kPsgCacheSuffix[kPsgCacheChannels]
        { "_ch1", "_ch2", "_ch3", "_noise" };
    for (int i = 0; i < kPsgCacheChannels; ++i)
    {
        const juce::String s { kPsgCacheSuffix[i] };
        psgAtkParam[i] = apvts.getRawParameterValue ("psg_atk" + s);
        psgDr1Param[i] = apvts.getRawParameterValue ("psg_dr1" + s);
        psgSusParam[i] = apvts.getRawParameterValue ("psg_sus" + s);
        psgDr2Param[i] = apvts.getRawParameterValue ("psg_dr2" + s);
        psgRrParam [i] = apvts.getRawParameterValue ("psg_rr"  + s);
        psgVelParam[i] = apvts.getRawParameterValue ("psg_vel" + s);
        psgVolParam[i] = apvts.getRawParameterValue ("psg_vol" + s);
        psgPanParam[i] = apvts.getRawParameterValue ("psg_pan" + s);
    }
    for (int t = 0; t < kPsgCacheToneChs; ++t)
    {
        const juce::String s { kPsgCacheSuffix[t] };
        psgGlideParam [t] = apvts.getRawParameterValue ("psg_glide"  + s);
        psgDetuneParam[t] = apvts.getRawParameterValue ("psg_detune" + s);
    }
    psgNoiseTypeParam = apvts.getRawParameterValue ("psg_noise_type");
    psgNoiseRateParam = apvts.getRawParameterValue ("psg_noise_rate");
    psgNoiseAutoParam = apvts.getRawParameterValue ("psg_noise_auto");

    // Task 09 — listen for `mode_select` changes to drive the default-load
    // on manual mode switches.
    apvts.addParameterListener ("mode_select", this);
    lastHandledMode = currentMode();
}

GenVstAudioProcessor::~GenVstAudioProcessor()
{
    apvts.removeParameterListener ("mode_select", this);
    cancelPendingUpdate();
    patchBrowser.shutdown();
}

void GenVstAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    paramCache.connect (apvts);
    voiceAllocator.prepare (sampleRate, samplesPerBlock);
    psgEngine.prepare (sampleRate, samplesPerBlock);
    telemetry.prepare (sampleRate);
    monoScratch.allocate ((size_t) juce::jmax (1, samplesPerBlock), true);

    decimator.prepare    (sampleRate, samplesPerBlock);
    fmDecimator.prepare  (sampleRate, samplesPerBlock);
    outputFilter.prepare (sampleRate);
    ladder.prepare       (sampleRate);

    // D mode dry-signal scratch — stereo, sized to the host's max block.
    inputCopyBuffer.setSize (2, juce::jmax (1, samplesPerBlock),
                             /*keepExistingContent*/ false,
                             /*clearExtraSpace*/    true,
                             /*avoidReallocating*/  false);
    inputCopyBuffer.clear();

    lastMode = currentMode();

    voiceAllocator.setVgmLogger (&vgmLogger);
    psgEngine.setVgmLogger      (&vgmLogger);
    vgmLogger.prepare           (sampleRate);

    const bool firstPrepare = ! patchBrowserInitialised;
    if (firstPrepare)
    {
        factoryRootPath = resolveFactoryRoot (wrapperType);
        patchBrowser.initialize (factoryRootPath);
        patchBrowserInitialised = true;
    }

    // Drain any state-restore payload now that the patch browser is live.
    // setStateInformation may have run before the JUCE wrapper set wrapperType
    // (so we couldn't initialise the browser there); the pending restore
    // queued during that call is processed here on the first prepareToPlay
    // after restore. Subsequent prepareToPlay calls are no-ops because
    // drainPendingStateRestore resets the optional.
    const bool hadStateRestore = pendingStateRestore.has_value();
    if (pendingStateRestore.has_value())
        drainPendingStateRestore();

    // Cold-start default preset load. When the plugin is freshly instantiated
    // with no host state to restore, neither the editor opening nor the user
    // moving a knob triggers any preset load — the listener on mode_select
    // only fires when the value *changes*, and mode_select starts at its
    // default (FM) on a fresh apvts, so handleAsyncUpdate's mode-mismatch
    // guard early-returns. Without this, the FM panel boots with every TL
    // at default, no algorithm topology, etc., and the user has to open the
    // preset browser before they hear anything resembling a real instrument.
    //
    // We gate on `firstPrepare` so a host pausing+resuming playback doesn't
    // re-load the default, and on `! hadStateRestore` so a project save
    // with no preset active (e.g. the user tweaked knobs without ever
    // opening the browser) preserves the user's tweaks instead of clobbering
    // them with the factory default.
    if (firstPrepare && ! hadStateRestore
        && currentMode() != Mode::D
        && activePathForMode (currentMode()).isEmpty())
    {
        // handleAsyncUpdate's first check is "mode unchanged → return".
        // Flip lastHandledMode to any other value so the listener pathway
        // actually fires the default-load branch when it runs on the
        // message thread. The branch reads currentMode() afresh, so the
        // value we put here is just a sentinel.
        lastHandledMode = (currentMode() == Mode::FM) ? Mode::SQ : Mode::FM;
        triggerAsyncUpdate();
    }
}

void GenVstAudioProcessor::releaseResources()
{
}

bool GenVstAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& mainOut = layouts.getMainOutputChannelSet();
    const auto& mainIn  = layouts.getMainInputChannelSet();

    // D mode needs an audio input bus (ADR-0021); FM and SQ ignore it but the
    // bus must still be declared so the host treats us as instrument-with-input.
    // Accept mono+mono and stereo+stereo only; reject anything else.
    if (mainOut == juce::AudioChannelSet::mono()
        && (mainIn == juce::AudioChannelSet::mono() || mainIn == juce::AudioChannelSet::disabled()))
        return true;
    if (mainOut == juce::AudioChannelSet::stereo()
        && (mainIn == juce::AudioChannelSet::stereo() || mainIn == juce::AudioChannelSet::disabled()))
        return true;
    return false;
}

GenVstAudioProcessor::Mode GenVstAudioProcessor::currentMode() const noexcept
{
    if (modeSelectParam == nullptr) return Mode::FM;
    const int idx = juce::jlimit (0, 2, juce::roundToInt (modeSelectParam->load()));
    return static_cast<Mode> (idx);
}

void GenVstAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                         juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples  = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    if (numChannels == 0 || numSamples == 0)
    {
        for (const auto metadata : midiMessages)
            dispatchMidi (metadata.getMessage());
        telemetry.finishBlock();
        return;
    }

    const Mode mode = currentMode();

    // D mode: snapshot the input bus BEFORE any rendering overwrites it. The
    // snapshot drives both the wet path (decimator/ladder) and the dry path
    // (dry/wet blend). For FM/SQ the input is unread, so we skip the copy.
    // Also drives the D-mode signal-presence threshold for the NOTE ON LED.
    float dInputPeak = 0.0f;
    if (mode == Mode::D)
    {
        const int copyChannels = juce::jmin (
            juce::jmin (numChannels, inputCopyBuffer.getNumChannels()),
            getTotalNumInputChannels());
        for (int ch = 0; ch < copyChannels; ++ch)
        {
            inputCopyBuffer.copyFrom (ch, 0, buffer, ch, 0, numSamples);
            const float chPeak = inputCopyBuffer.getMagnitude (ch, 0, numSamples);
            if (chPeak > dInputPeak) dInputPeak = chPeak;
        }
        // Zero any input channels we don't have host data for so the dry mix
        // for missing channels reads as silence rather than uninitialised.
        for (int ch = copyChannels; ch < inputCopyBuffer.getNumChannels(); ++ch)
            inputCopyBuffer.clear (ch, 0, numSamples);
    }

    // Drain the patch-delivery queue (one queue serves the single FM patch).
    // The browser is wired into FM mode; Task 09 introduces the per-mode
    // routing with a Tag enum. For now any queued patch maps into the FM
    // apvts via paramCache.readPatch below — the audio-thread atomic stores
    // re-emerge as the next paramCache.readPatch sees the updated values.
    //
    // TL / SL are stored in Patch as **hardware attenuation** (0 = loudest,
    // max = silent) for TFI/VGI/DMP/Y12/OPM round-trip; the apvts surface
    // exposes the inverted *level* (02-fm-synthesis.md *UI level vs hardware
    // attenuation*). Flip on the way into apvts; the symmetric inversion in
    // paramCache.readPatch flips it back when the audio thread builds the
    // Patch each block.
    patchBrowser.drainAudioThreadQueue (
        [this] (int /* part */, const Patch& p)
        {
            for (int d = 0; d < FmParamCache::kNumOpParams; ++d)
            {
                const std::string_view id { kOpParams[d].id };
                const bool invert = (id == "tl" || id == "sl");
                const int  maxAtt = (id == "tl") ? 127 : 15;
                for (int op = 0; op < FmParamCache::kNumOps; ++op)
                {
                    if (auto* ptr = paramCache.opParam[d][op])
                    {
                        const int raw = static_cast<int> ((p.*(kOpParams[d].field))[op]);
                        const int storeValue = invert
                            ? FmRegisterMap::levelToAttenuation (raw, maxAtt)
                            : raw;
                        ptr->store (static_cast<float> (storeValue),
                                    std::memory_order_relaxed);
                    }
                }
            }
            for (int d = 0; d < FmParamCache::kNumPartParams; ++d)
                if (auto* ptr = paramCache.partParam[d])
                    ptr->store (static_cast<float> (p.*(kPartParams[d].field)),
                                std::memory_order_relaxed);
        });

    // VGM logger ticks once per block.
    vgmLogger.recordWaitSamples (numSamples);

    // Sample-accurate iteration: render gaps between MIDI events, then dispatch.
    const auto renderGap = [this, &buffer, mode] (int start, int n)
    {
        if (n <= 0) return;
        switch (mode)
        {
            case Mode::FM: renderFmBlock (buffer, start, n); break;
            case Mode::SQ: renderSqBlock (buffer, start, n); break;
            case Mode::D:  renderDBlock  (buffer, start, n); break;
        }
    };

    int cursor = 0;
    for (const auto metadata : midiMessages)
    {
        const int eventTime = juce::jlimit (0, numSamples, metadata.samplePosition);
        if (eventTime > cursor)
        {
            renderGap (cursor, eventTime - cursor);
            cursor = eventTime;
        }
        dispatchMidi (metadata.getMessage());
    }
    if (cursor < numSamples)
        renderGap (cursor, numSamples - cursor);

    // Silence any output channels beyond stereo.
    for (int ch = 2; ch < numChannels; ++ch)
        buffer.clear (ch, 0, numSamples);

    // Whole-block DSP. Ladder applies to FM (over the summed voices) and was
    // already applied inside renderDBlock for D mode (before dry/wet blend),
    // so it must NOT be re-applied to the buffer post-blend. SQ skips ladder
    // entirely (the PSG bypasses the YM2612 DAC on real hardware — ADR-0024).
    // HARDWARE STRICT forces both toggles on regardless of their apvts value
    // (Settings view 6 — the UI greys + locks the header toggles, the audio
    // path enforces it here so a stale apvts read can't bypass strict
    // semantics).
    const bool hwStrict = hardwareStrictParam != nullptr
                            && hardwareStrictParam->load() > 0.5f;
    const bool ladderOn = hwStrict
                            || (ladderEffectParam != nullptr && ladderEffectParam->load() > 0.5f);
    const bool filterOn = hwStrict
                            || (outputFilterParam != nullptr && outputFilterParam->load() > 0.5f);

    if (mode == Mode::FM)
        ladder.process (buffer, ladderOn);

    outputFilter.process (buffer, filterOn);

    // Mode-change crossfade: ramp the whole output 0 -> 1 across the block on
    // the first block where the mode differs from the previous block's mode.
    if (mode != lastMode)
    {
        const int rampChannels = juce::jmin (2, numChannels);
        const float denom = static_cast<float> (juce::jmax (1, numSamples - 1));
        for (int ch = 0; ch < rampChannels; ++ch)
        {
            float* data = buffer.getWritePointer (ch);
            for (int i = 0; i < numSamples; ++i)
                data[i] *= static_cast<float> (i) / denom;
        }
        lastMode = mode;
    }

    // Apply master volume + soft-clip across the whole block.
    const float gain = masterVolumeParam != nullptr ? masterVolumeParam->load() : 1.0f;
    for (int ch = 0; ch < juce::jmin (2, numChannels); ++ch)
    {
        float* data = buffer.getWritePointer (ch);
        for (int i = 0; i < numSamples; ++i)
            data[i] = softClip (data[i] * gain);
    }

    // Telemetry: L/R VU + noteOn signal-presence flag. In D mode the LED
    // tracks input-level above -60 dBFS (~0.001) so the user sees "audio
    // present" without needing a MIDI trigger.
    const float* L = buffer.getReadPointer (0);
    const float* R = numChannels > 1 ? buffer.getReadPointer (1) : L;
    telemetry.pushSamples (L, R, numSamples);
    if (mode == Mode::D)
        telemetry.setNoteOn (dInputPeak > 0.001f);
    telemetry.finishBlock();
}

void GenVstAudioProcessor::renderFmBlock (juce::AudioBuffer<float>& buffer,
                                          int startSample, int numSamples)
{
    if (numSamples <= 0) return;

    // Bring the voice allocator's polyphony / legato state in sync with the
    // FM panel's apvts before the dirty-diff propagation. Cheap — three
    // atomic reads + a struct copy — but must run before noteOn dispatch in
    // future MIDI events would already have used a stale mode.
    pushPolyphonyParameters();

    // Snapshot the current single-engine patch and propagate to active voices.
    paramCache.readPatch (currentPatch);

    // MW → PMS layering at fixed full depth (02-fm-synthesis.md *Global MW →
    // PMS*; the v2 first-pass `mw_to_pms` scaler was dropped post-mockup-
    // review). The patch's `pms` is the base; MW contributes up to 7 on top.
    if (modWheelMirrorParam != nullptr)
    {
        const float mwNorm = juce::jlimit (0.0f, 1.0f, modWheelMirrorParam->load());
        const int   pmsAdd = juce::roundToInt (7.0f * mwNorm);
        currentPatch.pms = static_cast<std::uint8_t> (
            juce::jlimit (0, 7, static_cast<int> (currentPatch.pms) + pmsAdd));
    }

    // AFTERTOUCH routing (Settings view 6): channel pressure either rides
    // PMS (vibrato depth) or attenuates the carrier op(s) per the active
    // algorithm's carrier set. Off = no contribution.
    const int atTarget = aftertouchTargetParam != nullptr
                           ? juce::jlimit (0, 2, juce::roundToInt (aftertouchTargetParam->load()))
                           : 1;
    const float atPressure = channelPressureNorm.load (std::memory_order_relaxed);
    if (atPressure > 0.0f)
    {
        if (atTarget == 1)   // LFO PMS — layered on top of any MW contribution.
        {
            const int atPmsAdd = juce::roundToInt (7.0f * atPressure);
            currentPatch.pms = static_cast<std::uint8_t> (
                juce::jlimit (0, 7, static_cast<int> (currentPatch.pms) + atPmsAdd));
        }
        else if (atTarget == 2)   // Carrier TL — only attenuate carrier ops.
        {
            const int algIdx = juce::jlimit (0, 7, static_cast<int> (currentPatch.alg));
            const std::uint8_t mask = kCarrierMaskForAlg[algIdx];
            // currentPatch.tl[op] is hardware attenuation here (0 = loudest;
            // readPatch already inverted from apvts level). Pressure pushes
            // it up toward silence (127).
            const int addAtten = juce::roundToInt (127.0f * atPressure);
            for (int op = 0; op < FmParamCache::kNumOps; ++op)
            {
                if ((mask & (1u << op)) == 0) continue;
                const int cur = static_cast<int> (currentPatch.tl[op]);
                currentPatch.tl[op] = static_cast<std::uint8_t> (
                    juce::jlimit (0, 127, cur + addAtten));
            }
        }
    }

    const bool velToTl = velocityToTlParam != nullptr && velocityToTlParam->load() > 0.5f;
    voiceAllocator.updateActiveVoicesForPart (0, currentPatch, velToTl);

    // Per-block pitch-bend application from the apvts mirror. This catches
    // both UI drag and DAW automation of the pitch_bend_value parameter
    // (which neither went through handlePitchBend nor reached the voices
    // before). The MIDI dispatch in handlePitchBend still writes the mirror
    // *and* calls voiceAllocator.setPitchBend immediately for low-latency
    // MIDI response, so the value we read here is up-to-date on the very
    // next block. setPitchBend is dirty-diff'd inside the voice, so calling
    // it every block when the bend hasn't changed costs nothing.
    if (pitchBendMirrorParam != nullptr && pitchBendRangeParam != nullptr)
    {
        const float bendNorm = juce::jlimit (-1.0f, 1.0f, pitchBendMirrorParam->load());
        const int   range    = juce::jlimit (1, 12, juce::roundToInt (pitchBendRangeParam->load()));
        const double semitones = static_cast<double> (bendNorm) * static_cast<double> (range);
        voiceAllocator.setPitchBend (0, semitones, currentPatch, velToTl);
    }

    const int numChannels = buffer.getNumChannels();
    float* left  = buffer.getWritePointer (0) + startSample;
    float* right = numChannels > 1 ? buffer.getWritePointer (1) + startSample
                                   : monoScratch.get();

    // Clear destinations (the engines render-add into them).
    std::fill_n (left, numSamples, 0.0f);
    if (numChannels > 1) std::fill_n (right, numSamples, 0.0f);

    // Idle-silence clamp. When no voice is sounding (no Active or Released)
    // we skip the voice-render path entirely and leave the output cleared
    // above. The reason isn't pure efficiency — it's an audible background
    // hiss: ymfm's idle output isn't a perfect zero (the chip's internal
    // phase accumulators tick regardless of envelope state), and that
    // LSB-level residue gets amplified by the LadderEffect's 8-bit
    // quantizer below into audible noise. SQ mode doesn't hit this because
    // Ladder is FM-only.
    if (voiceAllocator.hasAudibleVoice())
        voiceAllocator.render (left, right, numSamples);

    // FM DAC prescaler — sweeps the YM2612 DAC clock divider on the voice
    // summation bus, before the ladder / output-filter stages
    // (02-fm-synthesis.md § *DAC Prescaler (FM mode)*). Bypassed at 0.
    const float fmPrescaler01 = currentPatch.fm_dac_prescaler;
    if (fmPrescaler01 > 0.0f)
    {
        juce::AudioBuffer<float> subView (buffer.getArrayOfWritePointers(),
                                          juce::jmin (2, numChannels),
                                          startSample, numSamples);
        fmDecimator.process (subView, fmPrescaler01);
    }
}

void GenVstAudioProcessor::pushPolyphonyParameters()
{
    // poly_voices clamps the active pool size; note_mode toggles RETRIG /
    // LEGATO. mono = poly_voices == 1 (07-feature-spec.md *Polyphony*).
    // HARDWARE STRICT (Settings → view 6) clamps the upper end to 6 to
    // match the YM2612's hardware channel count.
    const bool hwStrict   = hardwareStrictParam != nullptr
                              && hardwareStrictParam->load() > 0.5f;
    const int  polyCeil   = hwStrict ? 6 : 16;
    const int  polyVoices = polyVoicesParam != nullptr
                              ? juce::jlimit (1, polyCeil, juce::roundToInt (polyVoicesParam->load()))
                              : polyCeil;
    const bool legato     = noteModeParam   != nullptr && noteModeParam->load() > 0.5f;

    voiceAllocator.setVoiceCount (polyVoices);

    VoiceAllocator::PartPolyMode m;
    if (polyVoices == 1)
    {
        m.mode       = VoiceAllocator::PartPolyMode::Mode::Mono;
        m.monoLegato = legato;
    }
    else
    {
        m.mode       = VoiceAllocator::PartPolyMode::Mode::Poly;
        m.monoLegato = false;   // poly LEGATO same-note re-key not in MVP scope
    }
    voiceAllocator.setPartMode (0, m);
}

void GenVstAudioProcessor::renderSqBlock (juce::AudioBuffer<float>& buffer,
                                          int startSample, int numSamples)
{
    if (numSamples <= 0) return;

    // Snapshot apvts → engine each block. The SN76489Engine setters
    // (envelope rates, vel, volume, pan, noise mode, glide) are all
    // contract-bound to be pushed by the processor every block per
    // SN76489Engine.h:128-133 — without this push the engine sits at
    // defaults and the SQ panel + .psg presets have no audible effect
    // on envelope shape, stereo image, or noise voicing.
    for (int ch = 0; ch < kPsgCacheChannels; ++ch)
    {
        const auto loadInt = [] (const std::atomic<float>* p) noexcept
        {
            return p != nullptr ? juce::roundToInt (p->load()) : 0;
        };
        const auto loadFloat = [] (const std::atomic<float>* p, float fb) noexcept
        {
            return p != nullptr ? p->load() : fb;
        };

        psgEngine.setEnvelopeRates (ch,
                                    loadInt (psgAtkParam[ch]),
                                    loadInt (psgDr1Param[ch]),
                                    loadInt (psgSusParam[ch]),
                                    loadInt (psgDr2Param[ch]),
                                    loadInt (psgRrParam [ch]));
        psgEngine.setEnvelopeVel   (ch, loadFloat (psgVelParam[ch], 1.0f));
        psgEngine.setChannelVolume (ch, loadFloat (psgVolParam[ch], 1.0f));
        psgEngine.setChannelPan    (ch, loadFloat (psgPanParam[ch], 0.0f));
    }
    for (int t = 0; t < kPsgCacheToneChs; ++t)
    {
        const float ms = psgGlideParam[t] != nullptr ? psgGlideParam[t]->load() : 0.0f;
        psgEngine.setGlideTimeMs (t, static_cast<double> (ms));
        const float cents = psgDetuneParam[t] != nullptr ? psgDetuneParam[t]->load() : 0.0f;
        psgEngine.setToneDetuneCents (t, static_cast<double> (cents));
    }

    // Per-block pitch-bend application from the apvts mirror — same
    // rationale as the FM path above. Catches UI drag and DAW automation
    // of pitch_bend_value; MIDI dispatch still applies immediately for
    // low-latency response.
    if (pitchBendMirrorParam != nullptr && pitchBendRangeParam != nullptr)
    {
        const float bendNorm = juce::jlimit (-1.0f, 1.0f, pitchBendMirrorParam->load());
        const int   range    = juce::jlimit (1, 12, juce::roundToInt (pitchBendRangeParam->load()));
        const double semitones = static_cast<double> (bendNorm) * static_cast<double> (range);
        for (int t = 0; t < kPsgCacheToneChs; ++t)
            psgEngine.setPitchBendSemitones (t, semitones);
    }

    // apvts noise type choices are { "white", "periodic" } (0, 1).
    // SN76489Engine::setNoiseType convention is { periodic=0, white=1 }
    // (matches the hardware bit-3 encoding). Invert so engine receives
    // the right value.
    const int apvtsNoiseType = psgNoiseTypeParam != nullptr
                                 ? juce::roundToInt (psgNoiseTypeParam->load())
                                 : 0;
    psgEngine.setNoiseType (apvtsNoiseType == 0 ? 1 : 0);

    // apvts noise rate choices are { "low", "mid", "high", "ch2" } (0..3),
    // but SN76489 bits 2:1 of the noise control byte encode it as
    // { 00=high, 01=mid, 10=low, 11=ch2 } (03-psg-synthesis.md "Shift Rates").
    // The engine's `setNoiseShiftRate` writes its argument straight into
    // those bits, so translate logical index -> hardware bit pattern here.
    // Without this, picking "L" played HIGH noise and "H" played LOW.
    static constexpr int kNoiseRateApvtsToHw[4] { 0b10, 0b01, 0b00, 0b11 };
    const int apvtsNoiseRate = psgNoiseRateParam != nullptr
                                 ? juce::roundToInt (psgNoiseRateParam->load())
                                 : 1;
    psgEngine.setNoiseShiftRate (kNoiseRateApvtsToHw[juce::jlimit (0, 3, apvtsNoiseRate)]);
    psgEngine.setNoiseAutoMode  (psgNoiseAutoParam != nullptr
                                   && psgNoiseAutoParam->load() > 0.5f);

    const int numChannels = buffer.getNumChannels();
    float* left  = buffer.getWritePointer (0) + startSample;
    float* right = numChannels > 1 ? buffer.getWritePointer (1) + startSample
                                   : monoScratch.get();

    std::fill_n (left, numSamples, 0.0f);
    if (numChannels > 1) std::fill_n (right, numSamples, 0.0f);

    psgEngine.renderAdd (left, right, numSamples);
}

void GenVstAudioProcessor::renderDBlock (juce::AudioBuffer<float>& buffer,
                                         int startSample, int numSamples)
{
    if (numSamples <= 0) return;

    const int numChannels = juce::jmin (2, buffer.getNumChannels());
    if (numChannels <= 0) return;

    // 1. Seed the wet path from the dry input snapshot captured at the top of
    //    processBlock. We work in `buffer` (output) and read from
    //    `inputCopyBuffer` (dry).
    for (int ch = 0; ch < numChannels; ++ch)
        buffer.copyFrom (ch, startSample, inputCopyBuffer, ch, startSample, numSamples);

    // 2. Optional MONO collapse — average L/R into both channels.
    const bool mono = monoParam != nullptr && monoParam->load() > 0.5f;
    if (mono && numChannels >= 2)
    {
        float* L = buffer.getWritePointer (0) + startSample;
        float* R = buffer.getWritePointer (1) + startSample;
        for (int i = 0; i < numSamples; ++i)
        {
            const float m = 0.5f * (L[i] + R[i]);
            L[i] = m;
            R[i] = m;
        }
    }

    // Sub-block view so the in-place DSP modules see only this slice.
    juce::AudioBuffer<float> subView (buffer.getArrayOfWritePointers(),
                                      numChannels, startSample, numSamples);

    // 3. Decimator (sample-and-hold + 8-bit quantise).
    const float prescaler01 = prescalerParam != nullptr ? prescalerParam->load() : 0.0f;
    decimator.process (subView, prescaler01);

    // 4. Ladder Effect on the wet path (gated by the global toggle). Applied
    //    here rather than in processBlock so the dry signal blended below is
    //    the unprocessed input — design pipeline in 01-architecture.md
    //    *Render Pipeline (D mode)*.
    const bool ladderOn = ladderEffectParam != nullptr && ladderEffectParam->load() > 0.5f;
    ladder.process (subView, ladderOn);

    // 5. DRY/WET blend. `wet = 0` -> pure input; `wet = 1` -> pure processed.
    const float wet = dryWetParam != nullptr ? juce::jlimit (0.0f, 1.0f, dryWetParam->load()) : 1.0f;
    const float dry = 1.0f - wet;
    for (int ch = 0; ch < numChannels; ++ch)
    {
        float*       outData = buffer.getWritePointer      (ch) + startSample;
        const float* dryData = inputCopyBuffer.getReadPointer (ch) + startSample;
        for (int i = 0; i < numSamples; ++i)
            outData[i] = dry * dryData[i] + wet * outData[i];
    }
}

void GenVstAudioProcessor::dispatchMidi (const juce::MidiMessage& msg)
{
    if (msg.isNoteOn())
        handleNoteOn (msg.getNoteNumber(), msg.getVelocity());
    else if (msg.isNoteOff())
        handleNoteOff (msg.getNoteNumber());
    else if (msg.isPitchWheel())
        handlePitchBend (msg.getPitchWheelValue());
    else if (msg.isChannelPressure())
        handleChannelPressure (msg.getChannelPressureValue());
    else if (msg.isController())
        handleControlChange (msg.getControllerNumber(), msg.getControllerValue());
}

void GenVstAudioProcessor::handleChannelPressure (int value)
{
    // Snapshot the latest 0..127 pressure value, normalised. The actual
    // routing (LFO PMS vs Carrier TL vs Off) is read in renderFmBlock so
    // a single block applies whatever is currently selected — flipping the
    // Settings choice mid-hold takes effect on the next render block.
    const float norm = juce::jlimit (0.0f, 1.0f,
                                     static_cast<float> (value) / 127.0f);
    channelPressureNorm.store (norm, std::memory_order_relaxed);
}

void GenVstAudioProcessor::handleNoteOn (int note, int velocity)
{
    if (velocity <= 0)
    {
        handleNoteOff (note);
        return;
    }

    const Mode mode = currentMode();
    switch (mode)
    {
        case Mode::FM:
        {
            // Ensure the allocator's mode/poly state is current before the
            // dispatch — the user may have flipped LEGATO between blocks.
            pushPolyphonyParameters();
            paramCache.readPatch (currentPatch);
            const bool velToTl = velocityToTlParam != nullptr
                                   && velocityToTlParam->load() > 0.5f;

            // HARDWARE STRICT — FLOAT_MUL / AUTO_RETRIG drive the YM2612's
            // channel-3 special features and the chip only has ONE such
            // channel. With strict on, the second voice asking for those
            // modes silently falls back to INT_MUL
            // (07-feature-spec.md *Hardware strict*).
            Patch noteOnPatch = currentPatch;
            const bool hwStrict = hardwareStrictParam != nullptr
                                    && hardwareStrictParam->load() > 0.5f;
            if (hwStrict
                && (noteOnPatch.freq_ctrl_mode == 1 || noteOnPatch.freq_ctrl_mode == 2)
                && voiceAllocator.hasActiveVoiceUsingChannel3())
            {
                noteOnPatch.freq_ctrl_mode = 0;
            }
            voiceAllocator.noteOn (0, note, velocity, /*bend*/ 0.0,
                                   velToTl, noteOnPatch);
            telemetry.setNoteOn (true);
            break;
        }
        case Mode::SQ:
            psgEngine.noteOnTone (note, velocity);
            telemetry.setNoteOn (true);
            break;
        case Mode::D:
            // D mode has no note triggering.
            break;
    }
}

void GenVstAudioProcessor::handleNoteOff (int note)
{
    const Mode mode = currentMode();
    switch (mode)
    {
        case Mode::FM:
            voiceAllocator.noteOff (0, note, /*sustainHeld*/ false);
            telemetry.setNoteOn (voiceAllocator.numActiveVoices() > 0);
            break;
        case Mode::SQ:
            psgEngine.noteOffTone (note);
            telemetry.setNoteOn (false);
            break;
        case Mode::D:
            break;
    }
}

void GenVstAudioProcessor::handlePitchBend (int bend14bit)
{
    // MIDI state mirror — display-only apvts param the GLOBAL IN `midi-wheel`
    // PB widget binds to (08-ui-views.md view 2; 05-ui-ux.md
    // *C++→JS telemetry push*). Normalised to [-1, +1].
    if (pitchBendMirrorParam != nullptr)
        pitchBendMirrorParam->store (pitchBendNormalised (bend14bit),
                                     std::memory_order_relaxed);

    const int range = pitchBendRangeParam != nullptr
                        ? juce::jlimit (1, 12, juce::roundToInt (pitchBendRangeParam->load()))
                        : 2;
    const double semitones = pitchBendToSemitones (bend14bit, range);

    const Mode mode = currentMode();
    if (mode == Mode::FM)
    {
        paramCache.readPatch (currentPatch);
        const bool velToTl = velocityToTlParam != nullptr
                               && velocityToTlParam->load() > 0.5f;
        voiceAllocator.setPitchBend (0, semitones, currentPatch, velToTl);
    }
    else if (mode == Mode::SQ)
    {
        for (int t = 0; t < kPsgTones; ++t)
            psgEngine.setPitchBendSemitones (t, semitones);
    }
}

void GenVstAudioProcessor::handleControlChange (int cc, int value)
{
    const float norm127 = juce::jlimit (0.0f, 1.0f, static_cast<float> (value) / 127.0f);

    const auto setParamNorm = [this] (const juce::String& id, float v)
    {
        if (auto* param = apvts.getParameter (id))
        {
            param->beginChangeGesture();
            param->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, v));
            param->endChangeGesture();
        }
    };

    // CC 1 = Modulation Wheel. Two things to do:
    //   1. Mirror live CC value into the display-only mod_wheel_value apvts
    //      param so the GLOBAL IN MW wheel widget tracks it.
    //   2. MW → LFO PMS dispatch (02-fm-synthesis.md *Global MW → PMS*) — at
    //      fixed full depth in v2, no scaler. The PMS write happens through
    //      VoiceAllocator's normal dirty-diff on the next block: storing into
    //      the `pms` apvts here would clobber the patch's PMS setting, so
    //      instead we keep `pms` as the patch's baseline and add the MW
    //      contribution at register-write time. Voice already reads
    //      patch.pms verbatim, so for the MVP we approximate by clamping
    //      `pms_register = clamp(pms_param + 7 × mwCC / 127, 0, 7)`.
    //   The clean way (separate base + MW additive at FmRegisterMap) is left
    //   for a polish task; for now this matches the user-visible behaviour
    //   described in verification step 8 of the task.
    if (cc == 1)
    {
        if (modWheelMirrorParam != nullptr)
            modWheelMirrorParam->store (norm127, std::memory_order_relaxed);
        // The apvts `pms` reflects the patch's *base* PMS; the MW contribution
        // is layered at the register-write path. Trigger a per-block param
        // push via voice dirty-diff — handled implicitly each FM render block
        // (renderFmBlock calls updateActiveVoicesForPart), since `pms` is one
        // of the part params Voice writes on key-on / dirty-diff.
        return;
    }

    // CC 7 (Channel Volume) intentionally NOT routed to master_volume.
    // master_volume is a per-instance trim knob the user sets in the header;
    // CC 7 is host-track-level volume already covered by the DAW's channel
    // strip. Forwarding it overwrote the trim every time the host sent a
    // CC 7 (track-fader automation, controller defaults), making the VOL
    // knob appear to drift during playback. Users who want host automation
    // on master_volume can use the host's parameter automation lane.
    // CC 10 — pan; mapped to D-mode mono toggle off / centered for now — no
    // dedicated FM/SQ pan apvts in v2 MVP (per panel design). Silent skip.

    // CC 64 — sustain pedal. >=64 holds; <64 releases. FM only — SQ ignores.
    if (cc == 64)
    {
        // Sustain pedal handling is wired by the noteOff path's sustainHeld
        // flag, but the per-voice markSustained/clearSustained transition is
        // governed here. The v1 path tracked this via a per-part flag in
        // PluginProcessor; for v2 MVP we forward to allocator's
        // releaseSustained on pedal-up.
        if (value < 64)
            voiceAllocator.releaseSustained (0);
        return;
    }

    // CC 86 / 87 — output filter / ladder effect (header toggles).
    if (cc == 86) { setParamNorm ("output_filter", value >= 64 ? 1.0f : 0.0f); return; }
    if (cc == 87) { setParamNorm ("ladder_effect", value >= 64 ? 1.0f : 0.0f); return; }

    // CC 88 / 89 — freq_ctrl_mode + retrig_rate (07-feature-spec.md CC map).
    if (cc == 88)
    {
        // Quantise to the 3 choices.
        const int idx = juce::jlimit (0, 2, value / 42);
        setParamNorm ("freq_ctrl_mode", idx / 2.0f);
        return;
    }
    if (cc == 89)
    {
        // Scale CC 0..127 across the retrig_rate range 0..1023.
        const int rate = juce::roundToInt (norm127 * 1023.0f);
        setParamNorm ("retrig_rate", rate / 1023.0f);
        return;
    }
    // CC 90 — fm_dac_prescaler (0..1).
    if (cc == 90)
    {
        setParamNorm ("fm_dac_prescaler", norm127);
        return;
    }

    // CC 120 / 123 — All Sound Off / All Notes Off panic.
    if (cc == 120)
    {
        voiceAllocator.allSoundOff();
        psgEngine.reset();
        telemetry.setNoteOn (false);
        return;
    }
    if (cc == 123)
    {
        voiceAllocator.allNotesOff();
        psgEngine.reset();
        telemetry.setNoteOn (false);
        return;
    }

    // Per-op TL / MUL / DT / AR / DR / SR / RR / SL / KS / AMON CC bank
    // (07-feature-spec.md *MIDI CC Map* — TL CCs are now in *level* per the
    // v2 inversion). Mapped CC = base + opIndex with 4-op stride.
    struct OpCcDesc { int base; const char* id; int hi; };
    static const OpCcDesc kPerOpCcs[]
    {
        { 12, "tl",  127 },   //  12..15
        { 16, "mul",  15 },   //  16..19
        { 20, "dt",    6 },   //  20..23
        { 24, "ar",   31 },   //  24..27
        { 28, "dr",   31 },   //  28..31
        { 70, "sr",   31 },   //  70..73
        { 74, "rr",   15 },   //  74..77
        { 78, "sl",   15 },   //  78..81
        { 82, "ks",    3 },   //  82..85
        { 96, "amon",  1 },   //  96..99
    };
    for (const auto& desc : kPerOpCcs)
    {
        if (cc >= desc.base && cc < desc.base + 4)
        {
            const int op = cc - desc.base;
            const int v  = juce::jmap (value, 0, 127, 0, desc.hi);
            setParamNorm (juce::String (desc.id) + "_op" + juce::String (op + 1),
                          desc.hi == 0 ? 0.0f : static_cast<float> (v) / desc.hi);
            return;
        }
    }
}

void GenVstAudioProcessor::resetAllParametersToDefaults()
{
    // Walk every managed parameter and snap it to its juce::AudioParameter
    // default. setValueNotifyingHost takes the [0..1] normalised value, which
    // is what `getDefaultValue` returns — apvts already wraps the
    // RangedAudioParameters so they all expose the same protocol.
    // 08-ui-views.md view 6 *RESET ALL TO DEFAULTS*.
    for (auto* p : getParameters())
    {
        if (p == nullptr) continue;
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->getDefaultValue());
        p->endChangeGesture();
    }

    // Clear the active patch path on every multitimbral slot so the
    // header LCD reads as empty again (Task 09 will surface the patch path
    // through the LCD; for v0.2 the browser just owns it).
    for (int part = 0; part < genvst::PatchBrowser::kNumPartSlots; ++part)
        patchBrowser.clearActivePatchPath (part);
}

juce::AudioProcessorEditor* GenVstAudioProcessor::createEditor()
{
    return new GenVstAudioProcessorEditor (*this);
}

bool GenVstAudioProcessor::hasEditor() const                            { return true; }

const juce::String GenVstAudioProcessor::getName() const                { return "Gen VST"; }
bool GenVstAudioProcessor::acceptsMidi() const                          { return true; }
bool GenVstAudioProcessor::producesMidi() const                         { return false; }
double GenVstAudioProcessor::getTailLengthSeconds() const               { return 0.0; }

int GenVstAudioProcessor::getNumPrograms()                              { return 1; }
int GenVstAudioProcessor::getCurrentProgram()                           { return 0; }
void GenVstAudioProcessor::setCurrentProgram (int)                      {}
const juce::String GenVstAudioProcessor::getProgramName (int)           { return "Gen VST"; }
void GenVstAudioProcessor::changeProgramName (int, const juce::String&) {}

void GenVstAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // Collect Custom-kind roots from the browser to persist. The user's
    // factory / user-saved / user-imported roots are recreated automatically
    // on startup, so only the Custom ones need to survive in the project.
    std::vector<juce::String> customRoots;
    for (const auto& r : patchBrowser.roots())
    {
        if (r == nullptr || r->folder == nullptr) continue;
        if (r->kind != genvst::PatchRootKind::Custom)  continue;
        customRoots.push_back (r->folder->path);
    }

    auto xml = genvst::state::save (apvts.copyState(),
                                    activePathForMode (Mode::FM),
                                    activePathForMode (Mode::SQ),
                                    customRoots);
    if (xml != nullptr)
        copyXmlToBinary (*xml, destData);
}

void GenVstAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr)
        return;

    auto pending = genvst::state::restore (apvts, *xml);
    if (! pending.has_value())
    {
        // Anything that isn't a v2 <GenVstState/> envelope is treated as a
        // legacy / unknown byte stream and rejected without touching apvts.
        // The release notes warn that v1 projects don't migrate.
        emitStateRestoreToast (
            "warn", "Gen VST: legacy state ignored — re-save the project to upgrade.");
        return;
    }

    pendingStateRestore = std::move (*pending);
    // The first prepareToPlay drains pendingStateRestore — see comment on the
    // pendingStateRestore data member.
}

// =============================================================================
// Task 09 — tagged preset loading + mode-switch defaults
// =============================================================================

namespace
{
    // Helper: set a single apvts parameter via setValueNotifyingHost given an
    // already-clamped value in the parameter's native range. Mirrors
    // PsgPreset.cpp's setParamScaled.
    void setApvtsScaled (juce::AudioProcessorValueTreeState& apvts,
                         const juce::String& id, float scaledValue)
    {
        if (auto* p = apvts.getParameter (id))
        {
            const auto& r = p->getNormalisableRange();
            const float n = r.convertTo0to1 (juce::jlimit (r.start, r.end, scaledValue));
            p->beginChangeGesture();
            p->setValueNotifyingHost (n);
            p->endChangeGesture();
        }
    }

    // Convert a Mode enum to the apvts choice index.
    int modeToChoiceIndex (GenVstAudioProcessor::Mode m) noexcept
    {
        return static_cast<int> (m);
    }

    // FM param IDs + per-op variants — used to push a parsed Patch into the
    // apvts via setValueNotifyingHost (auto-mode-switch UI feedback path).
    struct FmApplyMap
    {
        const char* id;
        std::uint8_t (Patch::* field)[4];
        int  maxAtten;        // for tl/sl inversion
        bool invertForUi;     // tl / sl: hardware attenuation → UI level
    };

    constexpr FmApplyMap kFmOpApply[]
    {
        { "dt",   &Patch::dt,    6,  false },
        { "mul",  &Patch::mul,  15,  false },
        { "tl",   &Patch::tl,  127,  true  },
        { "ks",   &Patch::ks,    3,  false },
        { "ar",   &Patch::ar,   31,  false },
        { "dr",   &Patch::dr,   31,  false },
        { "sr",   &Patch::sr,   31,  false },
        { "rr",   &Patch::rr,   15,  false },
        { "sl",   &Patch::sl,   15,  true  },
        { "ssg",  &Patch::ssg,  15,  false },
        { "amon", &Patch::amon,  1,  false },
    };

    void applyFmPatchToApvts (juce::AudioProcessorValueTreeState& apvts, const Patch& p)
    {
        for (const auto& d : kFmOpApply)
        {
            for (int op = 0; op < 4; ++op)
            {
                const int raw = static_cast<int> ((p.*(d.field))[op]);
                const int value = d.invertForUi
                                    ? FmRegisterMap::levelToAttenuation (raw, d.maxAtten)
                                    : raw;
                setApvtsScaled (apvts,
                                juce::String (d.id) + "_op" + juce::String (op + 1),
                                static_cast<float> (value));
            }
        }
        setApvtsScaled (apvts, "alg",        (float) p.alg);
        setApvtsScaled (apvts, "fb",         (float) p.fb);
        setApvtsScaled (apvts, "ams",        (float) p.ams);
        setApvtsScaled (apvts, "pms",        (float) p.pms);
        setApvtsScaled (apvts, "lr",         (float) p.lr);
        setApvtsScaled (apvts, "lfo_enable", (float) p.lfo_enable);
        setApvtsScaled (apvts, "lfo_rate",   (float) p.lfo_rate);
    }
}

void GenVstAudioProcessor::parameterChanged (const juce::String& parameterID, float /*newValue*/)
{
    if (parameterID == "mode_select")
        triggerAsyncUpdate();
}

void GenVstAudioProcessor::handleAsyncUpdate()
{
    const Mode m = currentMode();
    if (m == lastHandledMode)
        return;
    lastHandledMode = m;

    if (m == Mode::D)
        return;   // D mode: host owns the apvts values (ADR-0021).

    const juce::String activePath = activePathForMode (m);
    if (activePath.isNotEmpty())
    {
        // Mode has a remembered patch (typical after state-restore + mode flip
        // back) — the apvts already carries the patch's params, so we mustn't
        // re-parse from disk and clobber any user tweaks. Just refresh the
        // header LCD via the patch-loaded callback so it picks up the patch
        // name instead of staying on whatever was last visible.
        if (patchLoadedCallback)
        {
            PatchLoadedNotifier note;
            note.name = juce::String (std::filesystem::path { activePath.toRawUTF8() }
                                          .stem().string());
            note.tag  = (m == Mode::FM) ? Tag::FM : Tag::SQ;
            note.path = activePath;
            patchLoadedCallback (note);
        }
        return;
    }

    const auto defaultPath = defaultPresetPathForMode (m);
    if (defaultPath.isEmpty())
        return;   // no default available (factory root not resolved yet).

    // Loading the default may auto-switch mode (it can't here — the path
    // matches the current mode by construction). Errors are silent: a
    // missing default is non-fatal; the panel just starts blank.
    loadPresetFromPath (defaultPath);
}

void GenVstAudioProcessor::setPatchLoadedNotifier (
    std::function<void (const PatchLoadedNotifier&)> cb)
{
    patchLoadedCallback = std::move (cb);
}

void GenVstAudioProcessor::setStateRestoreNotifier (
    std::function<void (const juce::String&, const juce::String&)> cb)
{
    stateRestoreToastCallback = std::move (cb);

    // Drain any toasts that queued before the editor registered. Typical when
    // setStateInformation runs before createEditor in the host's instantiation
    // flow (Reaper, Logic, etc.).
    if (stateRestoreToastCallback)
    {
        auto drained = std::move (pendingStateRestoreToasts);
        pendingStateRestoreToasts.clear();
        for (const auto& t : drained)
            stateRestoreToastCallback (t.first, t.second);
    }
}

void GenVstAudioProcessor::emitStateRestoreToast (const juce::String& level,
                                                  const juce::String& message)
{
    if (stateRestoreToastCallback)
        stateRestoreToastCallback (level, message);
    else
        pendingStateRestoreToasts.emplace_back (level, message);
}

void GenVstAudioProcessor::drainPendingStateRestore()
{
    if (! pendingStateRestore.has_value())
        return;

    // exchange-and-reset so a nested re-entry (shouldn't happen, but cheap
    // insurance) can't loop on the same payload.
    auto pending = std::move (*pendingStateRestore);
    pendingStateRestore.reset();

    // ---- Custom roots --------------------------------------------------------
    // Re-register every persisted custom-root path. addCustomRoot is a no-op
    // for paths that already resolved (e.g. the user re-saved the project on
    // a machine where the folder exists), so we just check the return value
    // and toast on failure.
    for (const auto& path : pending.customRoots)
    {
        if (path.isEmpty()) continue;
        if (! juce::File (path).isDirectory())
        {
            emitStateRestoreToast ("warn",
                "Custom folder could not be loaded: " + path);
            continue;
        }
        const auto id = patchBrowser.addCustomRoot (path);
        if (id.isEmpty())
        {
            // The path resolves as a directory but addCustomRoot still
            // returned empty — most likely already-registered (dedup hit).
            // No toast for that; the user's intent is satisfied either way.
        }
    }

    // ---- Per-mode active patch paths ----------------------------------------
    // Label-only restore — the apvts already carries the patch's parameter
    // values (potentially with user tweaks since the patch was loaded), so
    // we mustn't re-parse the file. Just record the path + fire patchLoaded
    // for the current mode so the LCD picks up the patch name. The other
    // mode's patchLoaded fires when the user flips into it (see
    // handleAsyncUpdate's active-path branch).
    const auto applyPerModePath = [this] (Mode mode, const juce::String& path)
    {
        if (path.isEmpty()) return;
        if (! juce::File (path).existsAsFile())
        {
            emitStateRestoreToast ("warn",
                "Patch could not be loaded: " + path);
            return;
        }
        setActivePathForMode (mode, path);
    };
    applyPerModePath (Mode::FM, pending.activeFmPath);
    applyPerModePath (Mode::SQ, pending.activeSqPath);

    // Fire patchLoaded for the active mode's patch (if any) so the header LCD
    // updates. Other modes' LCD labels surface on first flip via handleAsyncUpdate.
    if (patchLoadedCallback)
    {
        const Mode m = currentMode();
        const juce::String activePath = activePathForMode (m);
        if (activePath.isNotEmpty() && m != Mode::D)
        {
            PatchLoadedNotifier note;
            note.name = juce::String (std::filesystem::path { activePath.toRawUTF8() }
                                          .stem().string());
            note.tag  = (m == Mode::FM) ? Tag::FM : Tag::SQ;
            note.path = activePath;
            patchLoadedCallback (note);
        }
    }

    // Refresh `lastHandledMode` so the listener-driven async update doesn't
    // immediately overwrite the just-restored state with a default-load. The
    // restored apvts already set mode_select; we've now installed the active
    // paths; subsequent manual mode flips fall through handleAsyncUpdate's
    // active-path branch (LCD refresh, no default-load).
    lastHandledMode = currentMode();
    cancelPendingUpdate();
}

juce::String GenVstAudioProcessor::defaultPresetPathForMode (Mode mode) const
{
    if (mode == Mode::D || factoryRootPath.empty())
        return {};

    namespace fs = std::filesystem;
    std::error_code ec;
    if (! fs::is_directory (factoryRootPath, ec))
        return {};

    if (mode == Mode::SQ)
    {
        const auto sqDefault = factoryRootPath / "sq" / "default.psg";
        if (fs::is_regular_file (sqDefault, ec))
            return juce::String (sqDefault.string());
        return {};
    }

    // FM — prefer extern/patches/bass.tfi, fall back to the first sorted
    // factory .tfi file.
    const auto bass = factoryRootPath / "bass.tfi";
    if (fs::is_regular_file (bass, ec))
        return juce::String (bass.string());

    std::vector<fs::path> candidates;
    for (const auto& entry : fs::directory_iterator (factoryRootPath, ec))
    {
        if (! entry.is_regular_file (ec)) continue;
        const auto ext = entry.path().extension().string();
        if (ext == ".tfi" || ext == ".TFI")
            candidates.push_back (entry.path());
    }
    std::sort (candidates.begin(), candidates.end(),
               [] (const fs::path& a, const fs::path& b)
               { return a.filename() < b.filename(); });
    if (! candidates.empty())
        return juce::String (candidates.front().string());

    return {};
}

juce::String GenVstAudioProcessor::activePathForMode (Mode mode) const
{
    const juce::ScopedLock lk (activePathLock);
    if (mode == Mode::FM) return activeFmPath;
    if (mode == Mode::SQ) return activeSqPath;
    return {};
}

void GenVstAudioProcessor::setActivePathForMode (Mode mode, const juce::String& path)
{
    const juce::ScopedLock lk (activePathLock);
    if      (mode == Mode::FM) activeFmPath = path;
    else if (mode == Mode::SQ) activeSqPath = path;
}

juce::String GenVstAudioProcessor::loadPresetFromPath (const juce::String& absolutePath)
{
    if (absolutePath.isEmpty())
        return "empty path";

    const std::filesystem::path fsPath { absolutePath.toRawUTF8() };
    const auto tagOpt = tagFromFile (fsPath);
    if (! tagOpt.has_value())
        return "unrecognised patch extension: " + fsPath.extension().string();

    const Tag tag = *tagOpt;

    // Step 1: parse on the message thread. If parsing fails we never touch
    // mode_select or any apvts param — the user's current state is
    // preserved.
    if (tag == Tag::FM)
    {
        const auto extLower = juce::String (fsPath.extension().string()).toLowerCase();
        PatchLoadResult result { std::nullopt, "unsupported FM extension" };
        if      (extLower == ".tfi") result = loadTFI (fsPath);
        else if (extLower == ".vgi") result = loadVGI (fsPath);
        else if (extLower == ".dmp") result = loadDMP (fsPath);
        else if (extLower == ".y12") result = loadY12 (fsPath);
        else if (extLower == ".opm") result = loadOPM (fsPath);

        if (! result.patch.has_value())
            return result.error.empty() ? juce::String ("unknown FM load error")
                                        : juce::String (result.error);

        return applyFmPatch (*result.patch, absolutePath);
    }

    // Tag::SQ — `.psg` loads through the native preset parser; `.dmp` in PSG
    // mode (byte 2 = 0) goes through the macro → ADSR import bridge per
    // ADR-0026. The bridge may produce a non-fatal warning (arpeggio / pitch
    // macro dropped) that we forward to the toast alongside the success
    // notification.
    const auto extLower = juce::String (fsPath.extension().string()).toLowerCase();
    const auto loaded = extLower == ".dmp" ? loadDmpPsg (fsPath)
                                           : loadPsgPreset (fsPath);
    if (! loaded.preset.has_value())
        return loaded.error.empty() ? juce::String ("unknown PSG load error")
                                    : juce::String (loaded.error);

    auto applyError = applyPsgPreset (*loaded.preset, absolutePath);
    if (! applyError.isEmpty())
        return applyError;

    // Surface the import-bridge toast so users know the load was lossy.
    // applyPsgPreset has already fired the success toast via patchLoadedCallback;
    // tack the per-load warning onto its name field so the editor's existing
    // notifier surface picks it up (one toast per load, not two).
    if (! loaded.warning.empty() && patchLoadedCallback)
    {
        PatchLoadedNotifier note;
        note.name = juce::String ("Imported as SQ preset (DMP PSG approximation). ")
                  + juce::String (loaded.warning);
        note.tag  = Tag::SQ;
        note.path = absolutePath;
        patchLoadedCallback (note);
    }
    else if (extLower == ".dmp" && patchLoadedCallback)
    {
        PatchLoadedNotifier note;
        note.name = "Imported as SQ preset (DMP PSG approximation).";
        note.tag  = Tag::SQ;
        note.path = absolutePath;
        patchLoadedCallback (note);
    }

    return {};
}

juce::String GenVstAudioProcessor::applyFmPatch (const Patch& patch, const juce::String& absolutePath)
{
    // Step 1: record the active path BEFORE flipping mode_select. The
    // mode-switch listener (handleAsyncUpdate) checks the active path to
    // decide whether to fall back to the default — having the path in place
    // first prevents a default-load racing the real apply.
    setActivePathForMode (Mode::FM, absolutePath);

    // Step 2: flip mode_select if needed. Use setValueNotifyingHost so the
    // mode pill UI updates and the host sees the automation event.
    if (auto* modeParam = apvts.getParameter ("mode_select"))
    {
        const int idx = modeToChoiceIndex (Mode::FM);
        if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (modeParam))
        {
            if (choice->getIndex() != idx)
            {
                const int last = juce::jmax (0, choice->choices.size() - 1);
                const float n  = last == 0 ? 0.0f : static_cast<float> (idx) / static_cast<float> (last);
                modeParam->beginChangeGesture();
                modeParam->setValueNotifyingHost (n);
                modeParam->endChangeGesture();
            }
        }
    }

    // Step 3: apply the patch to apvts via setValueNotifyingHost so the FM
    // panel's bound widgets update on screen and the host sees the
    // automation events. The audio thread reads these values back through
    // paramCache.readPatch each render block. The legacy FIFO drain remains
    // wired in processBlock as a no-op for empty queues — kept dormant for
    // a future low-latency apply path.
    applyFmPatchToApvts (apvts, patch);

    // Step 4: fire the patch-loaded callback so the editor pushes the LCD
    // update.
    if (patchLoadedCallback)
    {
        PatchLoadedNotifier note;
        note.name = juce::String (patch.name.empty()
                                    ? std::filesystem::path { absolutePath.toRawUTF8() }
                                          .stem().string()
                                    : patch.name);
        note.tag  = Tag::FM;
        note.path = absolutePath;
        patchLoadedCallback (note);
    }

    return {};
}

juce::String GenVstAudioProcessor::applyPsgPreset (const PsgPreset& preset,
                                                    const juce::String& absolutePath)
{
    setActivePathForMode (Mode::SQ, absolutePath);

    if (auto* modeParam = apvts.getParameter ("mode_select"))
    {
        const int idx = modeToChoiceIndex (Mode::SQ);
        if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (modeParam))
        {
            if (choice->getIndex() != idx)
            {
                const int last = juce::jmax (0, choice->choices.size() - 1);
                const float n  = last == 0 ? 0.0f : static_cast<float> (idx) / static_cast<float> (last);
                modeParam->beginChangeGesture();
                modeParam->setValueNotifyingHost (n);
                modeParam->endChangeGesture();
            }
        }
    }

    applyPsgPresetToApvts (preset, apvts);

    if (patchLoadedCallback)
    {
        PatchLoadedNotifier note;
        note.name = juce::String (preset.name.empty()
                                    ? std::filesystem::path { absolutePath.toRawUTF8() }
                                          .stem().string()
                                    : preset.name);
        note.tag  = Tag::SQ;
        note.path = absolutePath;
        patchLoadedCallback (note);
    }

    return {};
}

juce::String GenVstAudioProcessor::savePresetForCurrentMode (const juce::String& name,
                                                              juce::String& outError)
{
    outError.clear();
    const Mode m = currentMode();
    if (m == Mode::D)
    {
        outError = "D mode has no preset format — host owns the state";
        return {};
    }

    if (m == Mode::FM)
    {
        Patch patch {};
        paramCache.readPatch (patch);
        patch.name = name.toStdString();
        const auto result = patchBrowser.savePatchAsTfi (patch, name);
        if (! result.error.empty())
        {
            outError = result.error;
            return {};
        }
        // The save itself doesn't change the active patch path (the file is
        // on disk for next time). Refresh the UI's notion of the loaded
        // patch name so the LCD shows the new name immediately.
        setActivePathForMode (Mode::FM, result.path);
        if (patchLoadedCallback)
        {
            patchLoadedCallback ({ name, Tag::FM, result.path });
        }
        return result.path;
    }

    // SQ — write a .psg into the user-saved root.
    const auto savedRoot = std::filesystem::path { juce::File::getSpecialLocation (
        juce::File::SpecialLocationType::userApplicationDataDirectory)
        .getChildFile ("GenVst").getChildFile ("patches").getChildFile ("saved")
        .getFullPathName().toRawUTF8() };
    std::error_code ec;
    std::filesystem::create_directories (savedRoot, ec);

    const auto safe = name.isEmpty() ? juce::String ("preset") : name;
    const auto dest = savedRoot / (safe.toStdString() + ".psg");

    const auto preset = readPsgPresetFromApvts (apvts, name.toStdString());
    auto err = savePsgPreset (preset, dest);
    if (! err.empty())
    {
        outError = err;
        return {};
    }

    // Refresh the user-saved root so the new .psg appears in the browser.
    (void) patchBrowser.rescanWritableRoots();

    const auto destStr = juce::String (dest.string());
    setActivePathForMode (Mode::SQ, destStr);
    if (patchLoadedCallback)
        patchLoadedCallback ({ name, Tag::SQ, destStr });
    return destStr;
}

juce::String GenVstAudioProcessor::exportPresetForCurrentMode (const juce::String& destinationPath)
{
    const Mode m = currentMode();
    if (m == Mode::D)
        return "D mode has no preset format";
    if (destinationPath.isEmpty())
        return "empty export path";

    const std::filesystem::path dest { destinationPath.toRawUTF8() };
    const auto ext = juce::String (dest.extension().string()).toLowerCase();

    if (m == Mode::FM)
    {
        if (ext != ".tfi" && ext != ".vgi")
            return "FM export requires .tfi or .vgi extension";
        Patch patch {};
        paramCache.readPatch (patch);
        patch.name = dest.stem().string();
        if (ext == ".tfi") return juce::String (exportTFI (patch, dest));
        return juce::String (exportVGI (patch, dest));
    }

    // SQ
    if (ext != ".psg")
        return "SQ export requires .psg extension";
    const auto preset = readPsgPresetFromApvts (apvts, dest.stem().string());
    return juce::String (savePsgPreset (preset, dest));
}

juce::String GenVstAudioProcessor::patchNavigate (int direction)
{
    const Mode m = currentMode();
    if (m == Mode::D)
        return "D mode has no presets to navigate";
    if (direction == 0)
        return {};

    const Tag targetTag = (m == Mode::SQ) ? Tag::SQ : Tag::FM;

    // Collect every preset across every root that matches the target tag.
    // The cached PatchEntry::tag is authoritative for resolved entries; for
    // any leftover `Pending` (a `.dmp` past the expand-time cap), peek via
    // tagFromFile so prev/next still finds it when it matches the target.
    struct Entry { juce::String name; juce::String path; };
    std::vector<Entry> entries;
    for (const auto& root : patchBrowser.roots())
    {
        if (root == nullptr || root->folder == nullptr) continue;
        std::function<void (const genvst::PatchFolder&)> walk;
        walk = [&] (const genvst::PatchFolder& f)
        {
            for (const auto& p : f.patches)
            {
                Tag effective = p.tag;
                if (effective == Tag::Pending)
                {
                    const std::filesystem::path fsP { p.path.toRawUTF8() };
                    const auto t = tagFromFile (fsP);
                    if (! t.has_value()) continue;
                    effective = *t;
                }
                if (effective == targetTag) entries.push_back ({ p.name, p.path });
            }
            for (const auto& sub : f.subfolders)
                if (sub != nullptr && sub->scanned)
                    walk (*sub);
        };
        walk (*root->folder);
    }

    if (entries.empty())
        return "no presets available in current mode";

    // Stable sort by name for predictable prev/next traversal.
    std::sort (entries.begin(), entries.end(),
               [] (const Entry& a, const Entry& b)
               { return a.name.compareIgnoreCase (b.name) < 0; });

    // Find the current preset's index (by absolute path); -1 if not found.
    const juce::String current = activePathForMode (m);
    int idx = -1;
    for (int i = 0; i < (int) entries.size(); ++i)
        if (entries[(std::size_t) i].path == current) { idx = i; break; }

    int nextIdx;
    if (idx < 0)
    {
        // No active preset yet — direction picks first / last entry.
        nextIdx = direction > 0 ? 0 : (int) entries.size() - 1;
    }
    else
    {
        nextIdx = idx + (direction > 0 ? 1 : -1);
        if (nextIdx < 0) nextIdx = (int) entries.size() - 1;
        if (nextIdx >= (int) entries.size()) nextIdx = 0;
    }

    return loadPresetFromPath (entries[(std::size_t) nextIdx].path);
}

juce::var GenVstAudioProcessor::listAllPresetsAsJson() const
{
    juce::Array<juce::var> out;
    for (const auto& root : patchBrowser.roots())
    {
        if (root == nullptr || root->folder == nullptr) continue;
        const auto rootId = root->id;

        std::function<void (const genvst::PatchFolder&)> walk;
        walk = [&] (const genvst::PatchFolder& f)
        {
            for (const auto& p : f.patches)
            {
                // Prefer the cached PatchEntry::tag — scanImmediateChildren
                // resolved any `.dmp` files via tagFromFile up to the per-
                // expand cap (ADR-0026). Anything still Pending here is a
                // `.dmp` that hasn't been resolved yet; surface it with a
                // neutral "Pending" badge so the UI shows a grey chip.
                const char* tagStr = (p.tag == Tag::SQ)      ? "SQ"
                                   : (p.tag == Tag::Pending) ? "Pending"
                                                              : "FM";

                auto* obj = new juce::DynamicObject();
                obj->setProperty ("name",       p.name);
                obj->setProperty ("path",       p.path);
                obj->setProperty ("tag",        juce::String (tagStr));
                obj->setProperty ("rootId",     rootId);
                obj->setProperty ("folderPath", f.path);
                out.add (juce::var (obj));
            }
            for (const auto& sub : f.subfolders)
                if (sub != nullptr && sub->scanned)
                    walk (*sub);
        };
        walk (*root->folder);
    }
    return juce::var (out);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new GenVstAudioProcessor();
}
