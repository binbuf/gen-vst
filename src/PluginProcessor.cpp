#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <string_view>

#include "FmRegisterMap.h"
#include "PluginState.h"

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
    for (const auto& d : kPartParams)
        layout.add (std::make_unique<juce::AudioParameterInt> (
            juce::ParameterID { d.id, 1 }, displayName (d.id, -1),
            d.lo, d.hi, d.lo));

    for (const auto& d : kOpParams)
        for (int op = 0; op < FmParamCache::kNumOps; ++op)
        {
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
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "note_mode", 1 }, "Note Mode",
        juce::StringArray { "RETRIG", "LEGATO" }, 0));
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
}

GenVstAudioProcessor::~GenVstAudioProcessor()
{
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

    if (! patchBrowserInitialised)
    {
        patchBrowser.initialize (resolveFactoryRoot (wrapperType));
        patchBrowserInitialised = true;
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
    const bool ladderOn = ladderEffectParam != nullptr && ladderEffectParam->load() > 0.5f;
    const bool filterOn = outputFilterParam != nullptr && outputFilterParam->load() > 0.5f;

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

    const bool velToTl = velocityToTlParam != nullptr && velocityToTlParam->load() > 0.5f;
    voiceAllocator.updateActiveVoicesForPart (0, currentPatch, velToTl);

    const int numChannels = buffer.getNumChannels();
    float* left  = buffer.getWritePointer (0) + startSample;
    float* right = numChannels > 1 ? buffer.getWritePointer (1) + startSample
                                   : monoScratch.get();

    // Clear destinations (the engines render-add into them).
    std::fill_n (left, numSamples, 0.0f);
    if (numChannels > 1) std::fill_n (right, numSamples, 0.0f);

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
    const int  polyVoices = polyVoicesParam != nullptr
                              ? juce::jlimit (1, 16, juce::roundToInt (polyVoicesParam->load()))
                              : 16;
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
    else if (msg.isController())
        handleControlChange (msg.getControllerNumber(), msg.getControllerValue());
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
            voiceAllocator.noteOn (0, note, velocity, /*bend*/ 0.0,
                                   velToTl, currentPatch);
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

    // CC 7 — channel volume → master_volume.
    if (cc == 7)
    {
        setParamNorm ("master_volume", norm127);
        return;
    }
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
const juce::String GenVstAudioProcessor::getProgramName (int)           { return {}; }
void GenVstAudioProcessor::changeProgramName (int, const juce::String&) {}

void GenVstAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = genvst::state::save (*this))
        copyXmlToBinary (*xml, destData);
}

void GenVstAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        genvst::state::restore (*this, *xml);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new GenVstAudioProcessor();
}
