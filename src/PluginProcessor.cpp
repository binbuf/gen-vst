#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <utility>

#include "PatchSystem.h"
#include "PluginState.h"

namespace
{
    // Resolve the factory-root path at runtime (ADR-0005). For Standalone we
    // use the platform data directory the install rule writes to; for VST3 /
    // AU / etc. the factory patches sit inside the bundle at
    // Contents/Resources/patches/. The exact layout differs per platform
    // (.vst3 on Windows/Linux, .vst3/.component on macOS, etc.), so we walk
    // upwards from the loaded binary file looking for Resources/patches.
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

        // Dev fallback (cmake --build, plugin not yet packaged):
        // GENVST_DEV_PATCH_DIR points at extern/patches/ in the source tree,
        // so a fresh build is playable without an install step.
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

    // Soft-clip guard: leaves |x| <= 0.9 untouched and saturates beyond toward
    // +/-1.0, so the output stays bounded even if the summed FM mix runs hot.
    inline float softClip (float x) noexcept
    {
        constexpr float threshold = 0.9f;
        constexpr float headroom  = 1.0f - threshold;
        if (x >  threshold) return  threshold + headroom * std::tanh ((x - threshold) / headroom);
        if (x < -threshold) return -threshold + headroom * std::tanh ((x + threshold) / headroom);
        return x;
    }

    // --- FM parameter schema --------------------------------------------------
    // The full per-part FM parameter set is generated from these two tables, so
    // ~300 parameters are declared in a loop rather than by hand. Each row maps
    // a parameter to the Patch field it mirrors and to its hardware range.

    struct OpParamDesc
    {
        const char* id;                       // "<id>_op<1-4>_part<1-6>"
        std::uint8_t (Patch::* field)[4];      // per-operator Patch array
        int lo, hi;
    };

    struct PartParamDesc
    {
        const char* id;                       // "<id>_part<1-6>"
        std::uint8_t Patch::* field;           // per-part Patch scalar
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
        { "lr",         &Patch::lr,         0, 3 },   // bit1 = L, bit0 = R
        { "lfo_enable", &Patch::lfo_enable, 0, 1 },
        { "lfo_rate",   &Patch::lfo_rate,   0, 7 },
    };

    static_assert (sizeof (kOpParams)   / sizeof (kOpParams[0])   == FmParamCache::kNumOpParams);
    static_assert (sizeof (kPartParams) / sizeof (kPartParams[0]) == FmParamCache::kNumPartParams);
    static_assert (VoiceAllocator::kNumParts == PartManager::kNumParts);

    // Parameter IDs follow 01-architecture.md "Parameter System":
    // "<name>_op<1-4>_part<1-6>" per operator, "<name>_part<1-6>" per part.
    juce::String opParamId (const char* id, int op, int part)
    {
        return juce::String (id) + "_op" + juce::String (op + 1)
                                 + "_part" + juce::String (part + 1);
    }

    juce::String partParamId (const char* id, int part)
    {
        return juce::String (id) + "_part" + juce::String (part + 1);
    }

    // Human-readable parameter name for the DAW automation list. op < 0 marks a
    // per-part parameter.
    juce::String displayName (const char* id, int op, int part)
    {
        juce::String name = juce::String (id).toUpperCase().replace ("_", " ");
        if (op >= 0)
            name += " Op" + juce::String (op + 1);
        return name + " Part" + juce::String (part + 1);
    }

    // Push a loaded patch into a part's apvts parameters. Message thread only.
    void writePatchToParams (juce::AudioProcessorValueTreeState& apvts,
                             int part, const Patch& patch)
    {
        const auto setParam = [&apvts] (const juce::String& id, int value, int lo, int hi)
        {
            if (auto* p = apvts.getParameter (id))
                p->setValueNotifyingHost (
                    p->convertTo0to1 (static_cast<float> (juce::jlimit (lo, hi, value))));
        };

        for (const auto& d : kOpParams)
            for (int op = 0; op < FmParamCache::kNumOps; ++op)
                setParam (opParamId (d.id, op, part), (patch.*(d.field))[op], d.lo, d.hi);

        for (const auto& d : kPartParams)
            setParam (partParamId (d.id, part), patch.*(d.field), d.lo, d.hi);
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout GenVstAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "master_gain", 1 },
        "Master Gain",
        juce::NormalisableRange<float> (0.0f, 1.0f),
        0.8f));

    // Global MIDI controls (Task 06). bend_range / aftertouch_target are
    // AudioParameterChoice so the host displays human-readable labels rather
    // than raw ints; the Choice index is what apvts stores atomically.
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "bend_range", 1 },
        "Pitch Bend Range",
        juce::StringArray { "+/-1", "+/-2", "+/-7", "+/-12" },
        1));   // default index = +/-2 (07-feature-spec.md "Pitch Bend")

    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "vel_to_tl", 1 },
        "Velocity -> Carrier TL",
        true));

    // True stereo (Task 25). On = leave L/R untouched, Off = sum L+R / 2 into
    // both channels at the tail of processBlock so the plugin output collapses
    // to mono. Default on per genny-ui's header label.
    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "true_stereo", 1 },
        "True Stereo",
        true));

    // UI preference — hover tooltips on/off. Persisted with the rest of the
    // state so the user's preference survives across sessions even though it
    // doesn't affect the audio path.
    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "tooltips_enabled", 1 },
        "Tooltips",
        true));

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "aftertouch_target", 1 },
        "Aftertouch Target",
        juce::StringArray { "Off", "LFO Depth", "Carrier TL" },
        1));   // default = LFO Depth, the MVP-chosen target

    // Task 13 — Settings parameters wired into the Settings modal now;
    // VOICE COUNT becomes functional in Task 15 and UI SCALE in Task 17.
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "voice_count", 1 },
        "Voice Count",
        juce::StringArray { "8", "12", "16" },
        2));   // default = 16 voices (07-feature-spec.md)
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "ui_scale", 1 },
        "UI Scale",
        juce::StringArray { "1x", "2x", "3x" },
        0));   // default = 1x (ADR-0017)

    // Per-part polyphony controls (Task 15 / view 10). Each FM part is
    // independently Poly / Mono / Unison; Mono picks Retrigger vs Legato;
    // Unison takes a cents spread (0..50, default 12 — view 10 spec). The
    // MVP-chosen Mono default is Retrigger (07-feature-spec.md open question).
    for (int part = 0; part < PartManager::kNumParts; ++part)
    {
        const juce::String suffix = "_part" + juce::String (part + 1);
        layout.add (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { "poly_mode" + suffix, 1 },
            "Poly Mode Part " + juce::String (part + 1),
            juce::StringArray { "Poly", "Mono", "Unison" },
            0));
        layout.add (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { "mono_glide" + suffix, 1 },
            "Mono Glide Part " + juce::String (part + 1),
            juce::StringArray { "Retrigger", "Legato" },
            0));
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { "unison_spread" + suffix, 1 },
            "Unison Spread Part " + juce::String (part + 1),
            juce::NormalisableRange<float> (0.0f, 50.0f),
            12.0f));
        // Task 28 — portamento / glide time in ms. Only audible in Mono+Legato
        // (glide between legato voices); 0 = instant (current behaviour).
        layout.add (std::make_unique<juce::AudioParameterInt> (
            juce::ParameterID { "glide_time" + suffix, 1 },
            "Glide Time Part " + juce::String (part + 1),
            0, 2000, 0));
    }

    // --- PSG (SN76489) parameters --------------------------------------------
    // 03-psg-synthesis.md: per-channel volume, pan, opt-in pitch bend; direct
    // noise control (type/rate + optional auto-mode); global mix level; PSG
    // layer toggle.
    static const juce::StringArray kPsgChannelLabels { "Ch1", "Ch2", "Ch3", "Noise" };
    for (int i = 0; i < SN76489Engine::kNumChannels; ++i)
    {
        const auto suffix = kPsgChannelLabels[i].toLowerCase();
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { "psg_vol_" + suffix, 1 },
            "PSG " + kPsgChannelLabels[i] + " Volume",
            juce::NormalisableRange<float> (0.0f, 1.0f), 1.0f));
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { "psg_pan_" + suffix, 1 },
            "PSG " + kPsgChannelLabels[i] + " Pan",
            juce::NormalisableRange<float> (-1.0f, 1.0f), 0.0f));
        layout.add (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID { "psg_bend_" + suffix, 1 },
            "PSG " + kPsgChannelLabels[i] + " Pitch Bend",
            i < SN76489Engine::kNumToneChs));   // tone channels default on
    }

    // Per-channel PSG envelope params (Task 23) — software ADSR shared by all
    // four channels (3 tone + 1 noise). Ranges + IDs map onto the existing FM
    // operator-panel widget so the UI can reuse it unchanged (Task 23 scope).
    // Audible effect today: ATK/DR1/SUS/DR2/RR + VEL drive the software
    // envelope; DETUNE/FREQ/KSR/SSG are visual stubs (cosmetic for now —
    // wiring them in is a follow-up task).
    for (int i = 0; i < SN76489Engine::kNumChannels; ++i)
    {
        const auto suffix = kPsgChannelLabels[i].toLowerCase();
        const auto displayPrefix = juce::String ("PSG ") + kPsgChannelLabels[i];

        auto addInt = [&] (const char* idStem, const char* labelStem, int lo, int hi, int def)
        {
            layout.add (std::make_unique<juce::AudioParameterInt> (
                juce::ParameterID { juce::String (idStem) + "_" + suffix, 1 },
                displayPrefix + " " + labelStem, lo, hi, def));
        };

        addInt ("psg_atk",    "ATK",    0, 31, 0);
        addInt ("psg_dr1",    "DR1",    0, 31, 0);
        addInt ("psg_sus",    "SUS",    0, 15, 0);
        addInt ("psg_dr2",    "DR2",    0, 31, 0);
        addInt ("psg_rr",     "RR",     0, 15, 0);
        addInt ("psg_detune", "DETUNE", 0, 6,  3);   // FM-widget compatible (-3..+3 idea)
        addInt ("psg_freq",   "FREQ",   0, 15, 1);   // FM-widget compatible (multiplier)
        addInt ("psg_ksr",    "KSR",    0, 3,  0);   // ENV SCALE on the widget
        addInt ("psg_ssg",    "SSG",    0, 15, 0);
        // VEL is the operator-panel's amon slot — a 0/1 toggle controlling
        // velocity sensitivity (1 = velocity scales peak; 0 = peak ignores
        // velocity).
        layout.add (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID { juce::String ("psg_vel_") + suffix, 1 },
            displayPrefix + " VEL", true));
    }

    // Task 28 — PSG tone-channel glide-time, mirror of the FM per-part glide.
    // Noise has no pitch, so it is omitted; DAC has no pitch either.
    for (int i = 0; i < SN76489Engine::kNumToneChs; ++i)
    {
        const auto suffix = kPsgChannelLabels[i].toLowerCase();   // "ch1".."ch3"
        layout.add (std::make_unique<juce::AudioParameterInt> (
            juce::ParameterID { "glide_time_psg_" + suffix, 1 },
            "PSG " + kPsgChannelLabels[i] + " Glide Time",
            0, 2000, 0));
    }

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "psg_noise_type", 1 },
        "PSG Noise Type",
        juce::StringArray { "Periodic", "White" }, 1));
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "psg_noise_rate", 1 },
        "PSG Noise Rate",
        juce::StringArray { "Low (N/512)", "Mid (N/1024)", "High (N/2048)", "Tone 3" }, 1));
    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "psg_noise_auto", 1 },
        "PSG Noise Auto Mode", false));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "psg_mix", 1 },
        "PSG Mix",
        juce::NormalisableRange<float> (0.0f, 1.0f), 0.8f));

    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "psg_layer", 1 },
        "PSG Layer on FM", false));

    // --- DAC parameters ------------------------------------------------------
    // 07-feature-spec.md "DAC Mode Specification": rate (8000/11025/22050),
    // one-shot / loop, level, enable.
    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "dac_enable", 1 },
        "DAC Enable", true));
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "dac_rate", 1 },
        "DAC Rate",
        juce::StringArray { "8000 Hz", "11025 Hz", "22050 Hz" }, 2));
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "dac_mode", 1 },
        "DAC Mode",
        juce::StringArray { "One-shot", "Loop" }, 0));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "dac_level", 1 },
        "DAC Level",
        juce::NormalisableRange<float> (0.0f, 1.0f), 1.0f));

    // The full per-part FM parameter set: every part gets the per-operator and
    // per-part channel parameters, generated from the schema tables so the
    // ~300 FM parameters are declared in a loop (01-architecture.md
    // "Parameter System"). All default to their range minimum; the dev patch
    // load and, later, the patch browser populate them.
    for (int part = 0; part < PartManager::kNumParts; ++part)
    {
        for (const auto& d : kOpParams)
            for (int op = 0; op < FmParamCache::kNumOps; ++op)
                layout.add (std::make_unique<juce::AudioParameterInt> (
                    juce::ParameterID { opParamId (d.id, op, part), 1 },
                    displayName (d.id, op, part),
                    d.lo, d.hi, d.lo));

        for (const auto& d : kPartParams)
            layout.add (std::make_unique<juce::AudioParameterInt> (
                juce::ParameterID { partParamId (d.id, part), 1 },
                displayName (d.id, -1, part),
                d.lo, d.hi, d.lo));
    }

    // --- Task 22 — Per-part rack routing params ------------------------------
    // The instrument rack (08-ui-views.md view 1 revised) gives every rack row
    // its own MIDI channel + transpose + range + detune + balance. Generated
    // alongside the per-part FM params using the same naming convention.
    // PSG channels and the DAC slot get the same fields under PSG/DAC-suffix
    // IDs so the rack UI can bind every row through the standard relay path.
    auto addRackParams = [&layout] (const juce::String& suffix,
                                    int defaultMidiCh,
                                    const juce::String& displaySuffix)
    {
        layout.add (std::make_unique<juce::AudioParameterInt> (
            juce::ParameterID { "midi_ch" + suffix, 1 },
            "MIDI Channel " + displaySuffix,
            0, 16, defaultMidiCh));   // 0 = off, 1..16 mapped
        layout.add (std::make_unique<juce::AudioParameterInt> (
            juce::ParameterID { "transpose_st" + suffix, 1 },
            "Transpose Semi " + displaySuffix,
            -24, 24, 0));
        layout.add (std::make_unique<juce::AudioParameterInt> (
            juce::ParameterID { "transpose_oct" + suffix, 1 },
            "Transpose Oct " + displaySuffix,
            -2, 2, 0));
        layout.add (std::make_unique<juce::AudioParameterInt> (
            juce::ParameterID { "note_lo" + suffix, 1 },
            "Note Lo " + displaySuffix,
            0, 127, 0));
        layout.add (std::make_unique<juce::AudioParameterInt> (
            juce::ParameterID { "note_hi" + suffix, 1 },
            "Note Hi " + displaySuffix,
            0, 127, 127));
        layout.add (std::make_unique<juce::AudioParameterInt> (
            juce::ParameterID { "detune_cents" + suffix, 1 },
            "Detune Cents " + displaySuffix,
            -100, 100, 0));
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { "balance" + suffix, 1 },
            "Balance " + displaySuffix,
            juce::NormalisableRange<float> (-1.0f, 1.0f), 0.0f));
    };

    // FM parts (parts 1..6 in apvts; rack widget uses only parts 1..5).
    for (int part = 0; part < PartManager::kNumParts; ++part)
    {
        const juce::String suffix = "_part" + juce::String (part + 1);
        addRackParams (suffix, part + 1, "Part " + juce::String (part + 1));
    }

    // PSG channels (rack SQ rows). Default MIDI channels match the existing
    // MidiRouter defaults (11..13 for tones, 14 for noise) so a freshly-loaded
    // plugin keeps the documented routing.
    static const std::array<const char*, SN76489Engine::kNumChannels> kPsgRackIds
        { "ch1", "ch2", "ch3", "noise" };
    static const std::array<const char*, SN76489Engine::kNumChannels> kPsgRackDisplay
        { "PSG 1", "PSG 2", "PSG 3", "PSG Noise" };
    static constexpr std::array<int, SN76489Engine::kNumChannels> kPsgDefaultCh
        { 11, 12, 13, 14 };
    for (int i = 0; i < SN76489Engine::kNumChannels; ++i)
    {
        const juce::String suffix = juce::String ("_psg_") + kPsgRackIds[(std::size_t) i];
        addRackParams (suffix, kPsgDefaultCh[(std::size_t) i],
                       juce::String (kPsgRackDisplay[(std::size_t) i]));
    }

    // DAC slot.
    addRackParams ("_dac", 16, "DAC");

   #if GENVST_DEV_SERVER
    // Scratch parameters for the widget gallery (ui/src/gallery.*). Every
    // core widget mounts against one of these so the gallery exercises the
    // full two-way binding path (apvts <-> WebSliderRelay/etc.) without
    // disturbing real synth parameters. Only present in dev-server builds,
    // never in shipped binaries — Task 10 deliverable.
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "gallery_knob", 1 },     "Gallery Knob",
        juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "gallery_slider", 1 },   "Gallery Slider",
        juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterInt> (
        juce::ParameterID { "gallery_readout", 1 },  "Gallery Readout",
        -99, 99, 0));
    layout.add (std::make_unique<juce::AudioParameterInt> (
        juce::ParameterID { "gallery_step", 1 },     "Gallery Step Field",
        1, 16, 1));
    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "gallery_toggle", 1 },   "Gallery Toggle", false));
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "gallery_section", 1 },  "Gallery Section",
        juce::StringArray { "FM", "SQ", "D" }, 0));
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "gallery_tabs", 1 },     "Gallery Tabs",
        juce::StringArray { "Presets", "Import" }, 0));
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "gallery_list", 1 },     "Gallery List",
        juce::StringArray { "Sine Lead",   "Gadget Bass",  "Perc Kit",
                            "Brass Stab",  "Shinobi Bass", "Organ",
                            "Saw Wave",    "Piano",        "Bell",
                            "Pad" }, 0));
   #endif

    return layout;
}

void FmParamCache::connect (juce::AudioProcessorValueTreeState& apvts)
{
    for (int part = 0; part < PartManager::kNumParts; ++part)
    {
        for (int d = 0; d < kNumOpParams; ++d)
            for (int op = 0; op < kNumOps; ++op)
                opParam[d][part][op] =
                    apvts.getRawParameterValue (opParamId (kOpParams[d].id, op, part));

        for (int d = 0; d < kNumPartParams; ++d)
            partParam[d][part] =
                apvts.getRawParameterValue (partParamId (kPartParams[d].id, part));
    }
}

void FmParamCache::readPatch (int part, Patch& dest) const noexcept
{
    for (int d = 0; d < kNumOpParams; ++d)
        for (int op = 0; op < kNumOps; ++op)
            (dest.*(kOpParams[d].field))[op] =
                static_cast<std::uint8_t> (juce::roundToInt (opParam[d][part][op]->load()));

    for (int d = 0; d < kNumPartParams; ++d)
        dest.*(kPartParams[d].field) =
            static_cast<std::uint8_t> (juce::roundToInt (partParam[d][part]->load()));
}

GenVstAudioProcessor::GenVstAudioProcessor()
    : juce::AudioProcessor (BusesProperties()
        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    masterGainParam       = apvts.getRawParameterValue ("master_gain");
    bendRangeParam        = apvts.getRawParameterValue ("bend_range");
    velToTlParam          = apvts.getRawParameterValue ("vel_to_tl");
    trueStereoParam       = apvts.getRawParameterValue ("true_stereo");
    aftertouchTargetParam = apvts.getRawParameterValue ("aftertouch_target");
    voiceCountParam       = apvts.getRawParameterValue ("voice_count");

    // Per-part polyphony pointers (Task 15 / view 10) + per-part glide-time
    // (Task 28).
    for (int part = 0; part < PartManager::kNumParts; ++part)
    {
        const juce::String suffix = "_part" + juce::String (part + 1);
        polyModeParam[(std::size_t) part]     = apvts.getRawParameterValue ("poly_mode"     + suffix);
        monoGlideParam[(std::size_t) part]    = apvts.getRawParameterValue ("mono_glide"    + suffix);
        unisonSpreadParam[(std::size_t) part] = apvts.getRawParameterValue ("unison_spread" + suffix);
        glideTimeParam[(std::size_t) part]    = apvts.getRawParameterValue ("glide_time"    + suffix);
    }

    // PSG / DAC raw pointers — looked up once so the audio thread never
    // touches the parameter map.
    psgDacParams.mix         = apvts.getRawParameterValue ("psg_mix");
    psgDacParams.noiseType   = apvts.getRawParameterValue ("psg_noise_type");
    psgDacParams.noiseRate   = apvts.getRawParameterValue ("psg_noise_rate");
    psgDacParams.noiseAuto   = apvts.getRawParameterValue ("psg_noise_auto");

    static const std::array<const char*, SN76489Engine::kNumChannels> kPsgIds
        { "ch1", "ch2", "ch3", "noise" };
    for (int i = 0; i < SN76489Engine::kNumChannels; ++i)
    {
        const juce::String suffix = juce::String ("_") + kPsgIds[(std::size_t) i];
        psgDacParams.volume[(std::size_t) i] =
            apvts.getRawParameterValue (juce::String ("psg_vol") + suffix);
        psgDacParams.pan[(std::size_t) i] =
            apvts.getRawParameterValue (juce::String ("psg_pan") + suffix);
        psgDacParams.bendOn[(std::size_t) i] =
            apvts.getRawParameterValue (juce::String ("psg_bend") + suffix);

        // Task 23 — per-channel envelope params, cached once.
        psgDacParams.atk[(std::size_t) i] = apvts.getRawParameterValue (juce::String ("psg_atk") + suffix);
        psgDacParams.dr1[(std::size_t) i] = apvts.getRawParameterValue (juce::String ("psg_dr1") + suffix);
        psgDacParams.sus[(std::size_t) i] = apvts.getRawParameterValue (juce::String ("psg_sus") + suffix);
        psgDacParams.dr2[(std::size_t) i] = apvts.getRawParameterValue (juce::String ("psg_dr2") + suffix);
        psgDacParams.rr [(std::size_t) i] = apvts.getRawParameterValue (juce::String ("psg_rr")  + suffix);
        psgDacParams.vel[(std::size_t) i] = apvts.getRawParameterValue (juce::String ("psg_vel") + suffix);
    }

    psgDacParams.dacEnable = apvts.getRawParameterValue ("dac_enable");
    psgDacParams.dacRate   = apvts.getRawParameterValue ("dac_rate");
    psgDacParams.dacMode   = apvts.getRawParameterValue ("dac_mode");
    psgDacParams.dacLevel  = apvts.getRawParameterValue ("dac_level");

    // Task 28 — PSG tone-channel glide-time pointers (noise omitted; no pitch).
    for (int i = 0; i < SN76489Engine::kNumToneChs; ++i)
        psgDacParams.glideTimeMs[(std::size_t) i] =
            apvts.getRawParameterValue (juce::String ("glide_time_psg_")
                                        + kPsgIds[(std::size_t) i]);

    // Task 22 — Per-rack-slot routing param pointers. Cached once so the
    // audio thread (and MidiRouter routing-table sync) never pay a parameter
    // map lookup.
    auto cacheRack = [&] (RackParams& dest, const juce::String& suffix)
    {
        dest.midiCh       = apvts.getRawParameterValue ("midi_ch"       + suffix);
        dest.transposeSt  = apvts.getRawParameterValue ("transpose_st"  + suffix);
        dest.transposeOct = apvts.getRawParameterValue ("transpose_oct" + suffix);
        dest.noteLo       = apvts.getRawParameterValue ("note_lo"       + suffix);
        dest.noteHi       = apvts.getRawParameterValue ("note_hi"       + suffix);
        dest.detuneCents  = apvts.getRawParameterValue ("detune_cents"  + suffix);
        dest.balance      = apvts.getRawParameterValue ("balance"       + suffix);
    };
    for (int part = 0; part < PartManager::kNumParts; ++part)
        cacheRack (fmRackParams[(std::size_t) part],
                   "_part" + juce::String (part + 1));
    static const std::array<const char*, SN76489Engine::kNumChannels> kPsgRackSuffixIds
        { "ch1", "ch2", "ch3", "noise" };
    for (int i = 0; i < SN76489Engine::kNumChannels; ++i)
        cacheRack (psgRackParams[(std::size_t) i],
                   juce::String ("_psg_") + kPsgRackSuffixIds[(std::size_t) i]);
    cacheRack (dacRackParams, "_dac");

    buildCcParamLookup();
    loadDevDacSample();

    // Sync the cached rack midi-channel params into the existing routing
    // table so MidiRouter::forEachDestination sees the user-edited channel.
    // Defaults match the legacy MidiRouter defaults (1..6, 11..14, 16), so
    // this is a no-op on first launch and a meaningful update after any apvts
    // edit / state restore.
    syncRackRoutingToTable();

    // Whenever an apvts param changes, the message thread is the writer; we
    // re-sync the routing table from the cached `midi_ch_*` pointers on the
    // next prepareToPlay / state-restore via syncRackRoutingToTable(). The
    // direct UI write path also calls it explicitly through the editor's
    // setRouting native function.
}

void GenVstAudioProcessor::buildCcParamLookup()
{
    for (int part = 0; part < PartManager::kNumParts; ++part)
    {
        for (int cc = 0; cc < 128; ++cc)
        {
            if (auto id = MidiRouter::ccToParamId (cc, part); id.has_value())
                ccParamLookup[(size_t) part][(size_t) cc] = apvts.getRawParameterValue (*id);
            else
                ccParamLookup[(size_t) part][(size_t) cc] = nullptr;
        }

        // Pan (CC 10) writes the 2-bit L/R output-enable field via its own
        // cached pointer — CC 10 isn't in the standard CC table because its
        // 3-zone mapping doesn't use the scaleCC formula.
        lrParamLookup[(size_t) part] = apvts.getRawParameterValue (partParamId ("lr", part));
    }
}

GenVstAudioProcessor::~GenVstAudioProcessor()
{
    // Stop the patch browser's background indexer before its owner destructs.
    patchBrowser.shutdown();
}

void GenVstAudioProcessor::applyPatchToPart (int part, const Patch& patch)
{
    partManager.loadPatch (part, patch);
    writePatchToParams (apvts, part, patch);
}

void GenVstAudioProcessor::applyPatchOnAudioThread (int part, const Patch& patch) noexcept
{
    // Atomic-stores into the same raw float pointers the dirty-diff in
    // paramCache.readPatch reads from below — so the per-block dirty-diff
    // picks up the new patch on this very block, with no setValueNotifyingHost
    // call (which would allocate / take locks). Mirrors the kOpParams /
    // kPartParams tables used to declare the apvts parameters.
    for (int d = 0; d < FmParamCache::kNumOpParams; ++d)
        for (int op = 0; op < FmParamCache::kNumOps; ++op)
            writeIntParam (paramCache.opParam[d][part][op],
                           (patch.*(kOpParams[d].field))[op]);

    for (int d = 0; d < FmParamCache::kNumPartParams; ++d)
        writeIntParam (paramCache.partParam[d][part],
                       patch.*(kPartParams[d].field));
}

void GenVstAudioProcessor::loadDevDacSample()
{
#ifdef GENVST_DEV_DAC_WAV
    const juce::File wav { GENVST_DEV_DAC_WAV };
    if (wav.existsAsFile())
        dacPlayer.loadWav (wav);
#endif
}

void GenVstAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    // Build the raw-pointer parameter cache (per part) and the voice pool.
    paramCache.connect (apvts);
    voiceAllocator.prepare (sampleRate, samplesPerBlock);
    psgEngine.prepare (sampleRate, samplesPerBlock);
    dacPlayer.prepare (sampleRate, samplesPerBlock);
    telemetry.prepare (sampleRate);
    monoScratch.allocate ((size_t) juce::jmax (1, samplesPerBlock), true);

    // The patch browser needs the plugin's wrapperType to find the factory
    // root (the bundle's Resources/patches/ vs the standalone data dir —
    // ADR-0005). wrapperType isn't valid in the constructor (the JUCE plugin
    // client sets it after createPluginFilter returns), so we defer first
    // initialisation to here. Idempotent — repeat prepareToPlay calls do not
    // re-scan the disk.
    if (! patchBrowserInitialised)
    {
        patchBrowser.initialize (resolveFactoryRoot (wrapperType));
        patchBrowserInitialised = true;

        // Dev wiring (Task 05/06 parity): until the patch browser UI ships
        // (Task 14), seed every part with "organ" if present and override
        // part 1 with "bass" — so a fresh launch sounds multitimbral. Skip
        // this when state was restored (Task 16) — the project's saved
        // patches must not be clobbered by the dev fallback.
        if (! stateRestored)
        {
            const auto findFactory = [this] (const juce::String& name) -> const Patch*
            {
                for (int i = 0; i < patchBrowser.numFactoryPatches(); ++i)
                    if (auto* p = patchBrowser.factoryPatchByIndex (i);
                        p != nullptr && juce::String (p->name).equalsIgnoreCase (name))
                        return p;
                return nullptr;
            };
            if (const Patch* organ = findFactory ("organ"))
                for (int part = 0; part < PartManager::kNumParts; ++part)
                    applyPatchToPart (part, *organ);
            if (const Patch* bass = findFactory ("bass"))
                applyPatchToPart (1, *bass);
        }
    }

    // Task 16: a setStateInformation that ran before the patch browser was
    // initialised stashed its custom-root + per-part patch reloads in
    // pendingStateRestore. Replay them now that the browser is ready.
    genvst::state::applyPendingPatchAndRootRestore (*this);
}

void GenVstAudioProcessor::pushPsgDacParameters()
{
    if (psgDacParams.mix == nullptr) return;

    psgEngine.setMixLevel (psgDacParams.mix->load());
    psgEngine.setNoiseType  (juce::roundToInt (psgDacParams.noiseType->load()));
    psgEngine.setNoiseShiftRate (juce::roundToInt (psgDacParams.noiseRate->load()));
    psgEngine.setNoiseAutoMode  (psgDacParams.noiseAuto->load() > 0.5f);

    for (int i = 0; i < SN76489Engine::kNumChannels; ++i)
    {
        psgEngine.setChannelVolume (i, psgDacParams.volume[(std::size_t) i]->load());
        psgEngine.setChannelPan    (i, psgDacParams.pan[(std::size_t) i]->load());
        psgEngine.setChannelBendEnabled (i, psgDacParams.bendOn[(std::size_t) i]->load() > 0.5f);

        // Task 28 — push per-tone-channel glide time (noise has no pitch and
        // no glide param). Out-of-range channels skip cleanly.
        if (i < SN76489Engine::kNumToneChs
            && psgDacParams.glideTimeMs[(std::size_t) i] != nullptr)
            psgEngine.setGlideTimeMs (i,
                juce::jlimit (0.0, 2000.0,
                              (double) psgDacParams.glideTimeMs[(std::size_t) i]->load()));

        // Task 23 — push the per-channel envelope ints/scalar into the
        // engine's PsgEnvelope each block. No-op when the apvts pointer is
        // null (defensive — they're built in the ctor and live for the
        // plugin's lifetime).
        if (psgDacParams.atk[(std::size_t) i] != nullptr)
            psgEngine.setEnvelopeRates (i,
                juce::roundToInt (psgDacParams.atk[(std::size_t) i]->load()),
                juce::roundToInt (psgDacParams.dr1[(std::size_t) i]->load()),
                juce::roundToInt (psgDacParams.sus[(std::size_t) i]->load()),
                juce::roundToInt (psgDacParams.dr2[(std::size_t) i]->load()),
                juce::roundToInt (psgDacParams.rr [(std::size_t) i]->load()));
        if (psgDacParams.vel[(std::size_t) i] != nullptr)
            psgEngine.setEnvelopeVel (i,
                psgDacParams.vel[(std::size_t) i]->load() > 0.5f ? 1.0f : 0.0f);
    }

    // DAC: enable / level on every block; rate triggers a PCM regeneration
    // (potential allocation), so only call setDacRate if the value actually
    // changed. Audio-thread allocation in setDacRate is documented as a
    // best-effort: changing the rate during active playback is a non-real-time
    // user action in practice (a UI param toggle).
    dacPlayer.setEnabled (psgDacParams.dacEnable->load() > 0.5f);
    dacPlayer.setLevel   (psgDacParams.dacLevel->load());
    dacPlayer.setMode (psgDacParams.dacMode->load() > 0.5f
                       ? DACPlayer::Mode::Loop
                       : DACPlayer::Mode::OneShot);

    static constexpr int kRates[] { 8000, 11025, 22050 };
    const int rateIdx = juce::jlimit (0, 2, juce::roundToInt (psgDacParams.dacRate->load()));
    const int desiredRate = kRates[rateIdx];
    if (desiredRate != dacPlayer.getDacRate())
        dacPlayer.setDacRate (desiredRate);
}

void GenVstAudioProcessor::pushPolyphonyParameters() noexcept
{
    using Mode = VoiceAllocator::PartPolyMode::Mode;

    for (int part = 0; part < PartManager::kNumParts; ++part)
    {
        VoiceAllocator::PartPolyMode m;
        if (polyModeParam[(std::size_t) part] != nullptr)
        {
            const int idx = juce::jlimit (0, 2,
                                juce::roundToInt (polyModeParam[(std::size_t) part]->load()));
            m.mode = static_cast<Mode> (idx);
        }
        if (monoGlideParam[(std::size_t) part] != nullptr)
            m.monoLegato = monoGlideParam[(std::size_t) part]->load() > 0.5f;
        if (unisonSpreadParam[(std::size_t) part] != nullptr)
            m.spreadCents = juce::jlimit (0.0, 50.0,
                                (double) unisonSpreadParam[(std::size_t) part]->load());
        // Task 28 — push the per-part glide-time. Only used by Mono+Legato
        // note-ons; Poly / Unison ignore the value.
        if (glideTimeParam[(std::size_t) part] != nullptr)
            m.glideTimeMs = juce::jlimit (0.0, 2000.0,
                                (double) glideTimeParam[(std::size_t) part]->load());

        voiceAllocator.setPartMode (part, m);
    }

    if (voiceCountParam != nullptr)
    {
        static constexpr int kVoiceCounts[] { 8, 12, 16 };
        const int idx = juce::jlimit (0, 2, juce::roundToInt (voiceCountParam->load()));
        voiceAllocator.setVoiceCount (kVoiceCounts[idx]);
    }
}

void GenVstAudioProcessor::releaseResources()
{
}

bool GenVstAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& mainOut = layouts.getMainOutputChannelSet();
    return mainOut == juce::AudioChannelSet::mono()
        || mainOut == juce::AudioChannelSet::stereo();
}

void GenVstAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                         juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples  = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    if (numChannels == 0 || numSamples == 0)
    {
        // Still consume MIDI so events don't pile up across silent blocks.
        for (const auto metadata : midiMessages)
            dispatchMidi (metadata.getMessage());

        // Step the VU envelope on the silent block too, so the meter falls to
        // zero rather than freezing at its last value when the host pumps
        // empty blocks (e.g. transport stopped).
        telemetry.finishBlock (voiceAllocator.activeVoiceMask());
        return;
    }

    // Drain the patch-delivery queue (04-patch-system.md "Audio Thread
    // Delivery"): each (part, Patch) was pushed by the message thread after a
    // patch file parsed successfully. Applying first means paramCache.readPatch
    // below sees the new values on this very block, so newly-started voices
    // pick up the new patch and sounding voices catch up via the dirty-diff.
    patchBrowser.drainAudioThreadQueue (
        [this] (int part, const Patch& p) { applyPatchOnAudioThread (part, p); });

    // Drain the preview queue (Task 14): the patch browser's *Preview* button
    // pushed (part, note, vel) here on the message thread. Drained right after
    // the patch queue so a preview click immediately after a load uses the
    // freshly-loaded patch values.
    drainPreviewQueue();

    // Snapshot every part's current parameters and seed every active voice
    // with the dirty-diff (catches DAW automation that landed between blocks).
    const bool velToTl = currentVelToTl();
    for (int part = 0; part < PartManager::kNumParts; ++part)
        paramCache.readPatch (part, partPatches[(size_t) part]);
    voiceAllocator.updateActiveVoices (partPatches, velToTl);

    // Push PSG / DAC parameter values down once per block — the engines
    // hold scalar mirrors so the per-sample inner loops don't re-read atomics.
    pushPsgDacParameters();

    // Push per-part polyphony mode + global voice-count cap before any MIDI
    // note dispatch — note-ons later in this block use the freshly-pushed
    // values (Task 15 / view 10; 07-feature-spec.md "Polyphony Modes").
    pushPolyphonyParameters();

    // Task 22 — Sync rack midi-channel params into the MidiRouter table so a
    // step-field edit on a rack row reaches the dispatch on the next block.
    // Cheap (11 atomic loads + ≤11 atomic stores) and only triggers the
    // rebuildChannelMask path when an apvts value actually changed.
    syncRackRoutingToTable();

    // Sample-accurate iteration (01-architecture.md "MIDI Pipeline"): for each
    // gap between consecutive MIDI events, render that exact sub-block, then
    // dispatch the event. Out-of-range timestamps (some hosts deliver events
    // at the block tail or end-of-block) are clamped to keep render lengths
    // non-negative.
    int cursor = 0;
    for (const auto metadata : midiMessages)
    {
        const int eventTime = juce::jlimit (0, numSamples, metadata.samplePosition);
        if (eventTime > cursor)
        {
            renderSubBlock (buffer, cursor, eventTime - cursor);
            cursor = eventTime;
        }
        dispatchMidi (metadata.getMessage());
    }
    if (cursor < numSamples)
        renderSubBlock (buffer, cursor, numSamples - cursor);

    // Silence any output channels beyond the stereo pair.
    for (int ch = 2; ch < numChannels; ++ch)
        buffer.clear (ch, 0, numSamples);

    // Task 25 — TRUE STEREO toggle. When off, collapse L+R to mono by writing
    // (L+R)/2 into both channels. Runs after the sub-block renders so every
    // upstream stereo image (PSG pan, FM L/R, DAC) is preserved up to this
    // point, then folded. Telemetry has already consumed the stereo signal in
    // pushSamples, so the meters still show stereo activity even on mono out.
    if (numChannels > 1 && trueStereoParam != nullptr
        && trueStereoParam->load() < 0.5f)
    {
        float* left  = buffer.getWritePointer (0);
        float* right = buffer.getWritePointer (1);
        for (int i = 0; i < numSamples; ++i)
        {
            const float sum = (left[i] + right[i]) * 0.5f;
            left[i] = sum;
            right[i] = sum;
        }
    }

    // Commit per-block telemetry: step the VU envelope, publish the snapshot,
    // record the voice-activity mask. Audio-thread → message-thread handoff
    // is via atomics inside telemetry — no allocation, no locks.
    telemetry.finishBlock (voiceAllocator.activeVoiceMask());
}

void GenVstAudioProcessor::renderSubBlock (juce::AudioBuffer<float>& buffer,
                                           int startSample, int numSamples)
{
    if (numSamples <= 0)
        return;

    const int numChannels = buffer.getNumChannels();
    float* left  = buffer.getWritePointer (0) + startSample;
    float* right = numChannels > 1 ? buffer.getWritePointer (1) + startSample
                                   : monoScratch.get();

    // FM voices + DAC at native rate -> resample once to host rate.
    voiceAllocator.render (left, right, numSamples, &dacPlayer);

    // PSG mixes in at host rate (it resamples internally — ADR-0011).
    psgEngine.renderAdd (left, right, numSamples);

    // Apply master gain + soft-clip. The clip indicator (Task 12) lights when
    // the pre-soft-clip signal exceeds 0 dBFS — i.e., the soft-clipper is
    // actively saturating, the audible "clipping" condition the user cares
    // about.
    const float gain = masterGainParam->load();
    bool clipDetected = false;
    for (int i = 0; i < numSamples; ++i)
    {
        const float pre = left[i] * gain;
        if (std::fabs (pre) > 1.0f) clipDetected = true;
        left[i] = softClip (pre);
    }

    if (numChannels > 1)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            const float pre = right[i] * gain;
            if (std::fabs (pre) > 1.0f) clipDetected = true;
            right[i] = softClip (pre);
        }
    }
    else
    {
        // Mono output: still write the mono channel into right so the scope
        // and VU read identical L/R rather than zeros on the right.
        for (int i = 0; i < numSamples; ++i)
            right[i] = left[i];
    }

    // Lock-free telemetry write — feeds the editor's ~30 Hz "meterData" push
    // (08-ui-views.md "Header meter bay").
    telemetry.pushSamples (left, right, numSamples, clipDetected);
}

void GenVstAudioProcessor::dispatchMidi (const juce::MidiMessage& msg)
{
    const int channel = msg.getChannel();

    if (msg.isNoteOn())
        handleNoteOn (channel, msg.getNoteNumber(), msg.getVelocity());
    else if (msg.isNoteOff())
        handleNoteOff (channel, msg.getNoteNumber());
    else if (msg.isPitchWheel())
        handlePitchBend (channel, msg.getPitchWheelValue());
    else if (msg.isChannelPressure())
        handleAftertouch (channel, msg.getChannelPressureValue());
    else if (msg.isProgramChange())
        handleProgramChange (channel, msg.getProgramChangeNumber());
    else if (msg.isController())
        handleControlChange (channel, msg.getControllerNumber(), msg.getControllerValue());
    // Note: isAllNotesOff / isAllSoundOff / isResetAllControllers are CCs
    // and handled inside handleControlChange. Aftertouch *key pressure* and
    // other rare messages are ignored.
}

void GenVstAudioProcessor::handleNoteOn (int channel, int note, int velocity)
{
    // A velocity-0 note-on is a note-off per the MIDI spec — JUCE's
    // isNoteOn / isNoteOff already filter on this, so we treat velocity > 0
    // arrivals here as keystrikes. Task 13: layering — one MIDI channel can
    // reach multiple destinations; we dispatch to each.
    //
    // Task 22: each rack row applies its own transpose + range filter +
    // detune-cents offset before the voice path. Range-filtered notes are
    // silently dropped (no voice taken, no PSG / DAC trigger). Slots that
    // the rack widget has not activated (the user hasn't pressed "+") also
    // skip the dispatch — clearing a row via "−" must immediately mute that
    // destination even though the apvts midi-channel value persists.
    midiRouter.forEachDestination (channel, [&] (MidiRouter::Destination dest)
    {
        if (dest.isFmPart())
        {
            const int part = dest.index;
            if (! partManager.isSlotActive ({ PartManager::InstrumentType::FM, part })) return;
            const int transposed = fmPartTransposedNote (part, note);
            if (! fmPartAcceptsNote (part, transposed)) return;
            const int finalNote = juce::jlimit (0, 127, transposed);

            paramCache.readPatch (part, noteOnPatch);
            const double effectiveBend = midiRouter.pitchBendSemitones (part)
                                       + fmPartDetuneSemitones (part);
            voiceAllocator.noteOn (part, finalNote, velocity,
                                   effectiveBend,
                                   currentVelToTl(), noteOnPatch);
        }
        else if (dest.isPsgTone())
        {
            const int psgCh = dest.index;
            if (! partManager.isSlotActive ({ PartManager::InstrumentType::SQ, psgCh })) return;
            // PSG rack params share the same suffix layout: psgRackParams[0..2]
            // for tones, [3] for noise. Apply transpose + range the same way
            // as the FM path so SQ rows behave consistently.
            const auto& rp = psgRackParams[(std::size_t) psgCh];
            const int st = (rp.transposeSt  != nullptr) ? juce::roundToInt (rp.transposeSt->load())  : 0;
            const int oc = (rp.transposeOct != nullptr) ? juce::roundToInt (rp.transposeOct->load()) : 0;
            const int lo = (rp.noteLo != nullptr) ? juce::roundToInt (rp.noteLo->load()) : 0;
            const int hi = (rp.noteHi != nullptr) ? juce::roundToInt (rp.noteHi->load()) : 127;
            const int transposed = MidiRouter::applyTranspose (note, st, oc);
            if (! MidiRouter::noteInRange (transposed, lo, hi)) return;
            psgEngine.noteOnTone (juce::jlimit (0, 127, transposed), velocity);
        }
        else if (dest.isPsgNoise())
        {
            if (! partManager.isSlotActive (
                { PartManager::InstrumentType::SQ, PartManager::kPsgNoiseSqSlot })) return;
            const auto& rp = psgRackParams[SN76489Engine::kNoiseCh];
            const int st = (rp.transposeSt  != nullptr) ? juce::roundToInt (rp.transposeSt->load())  : 0;
            const int oc = (rp.transposeOct != nullptr) ? juce::roundToInt (rp.transposeOct->load()) : 0;
            const int lo = (rp.noteLo != nullptr) ? juce::roundToInt (rp.noteLo->load()) : 0;
            const int hi = (rp.noteHi != nullptr) ? juce::roundToInt (rp.noteHi->load()) : 127;
            const int transposed = MidiRouter::applyTranspose (note, st, oc);
            if (! MidiRouter::noteInRange (transposed, lo, hi)) return;
            psgEngine.noteOnNoise (juce::jlimit (0, 127, transposed), velocity);
        }
        else if (dest.isDac())
        {
            if (! partManager.isSlotActive ({ PartManager::InstrumentType::D, 0 })) return;
            // DAC sample triggers honour the rack's note range too — a DAC kit
            // mapped to "C2..C3" stays silent on out-of-range notes.
            const auto& rp = dacRackParams;
            const int st = (rp.transposeSt  != nullptr) ? juce::roundToInt (rp.transposeSt->load())  : 0;
            const int oc = (rp.transposeOct != nullptr) ? juce::roundToInt (rp.transposeOct->load()) : 0;
            const int lo = (rp.noteLo != nullptr) ? juce::roundToInt (rp.noteLo->load()) : 0;
            const int hi = (rp.noteHi != nullptr) ? juce::roundToInt (rp.noteHi->load()) : 127;
            const int transposed = MidiRouter::applyTranspose (note, st, oc);
            if (! MidiRouter::noteInRange (transposed, lo, hi)) return;
            dacPlayer.trigger (juce::jlimit (0, 127, transposed), velocity);
        }
    });
}

void GenVstAudioProcessor::handleNoteOff (int channel, int note)
{
    midiRouter.forEachDestination (channel, [&] (MidiRouter::Destination dest)
    {
        if (dest.isFmPart())
        {
            const int part = dest.index;
            const int transposed = fmPartTransposedNote (part, note);
            const int finalNote = juce::jlimit (0, 127, transposed);
            voiceAllocator.noteOff (part, finalNote, midiRouter.sustainPedalHeld (part));
        }
        else if (dest.isPsgTone())
        {
            const int psgCh = dest.index;
            const auto& rp = psgRackParams[(std::size_t) psgCh];
            const int st = (rp.transposeSt  != nullptr) ? juce::roundToInt (rp.transposeSt->load())  : 0;
            const int oc = (rp.transposeOct != nullptr) ? juce::roundToInt (rp.transposeOct->load()) : 0;
            psgEngine.noteOffTone (juce::jlimit (0, 127,
                MidiRouter::applyTranspose (note, st, oc)));
        }
        else if (dest.isPsgNoise())
        {
            const auto& rp = psgRackParams[SN76489Engine::kNoiseCh];
            const int st = (rp.transposeSt  != nullptr) ? juce::roundToInt (rp.transposeSt->load())  : 0;
            const int oc = (rp.transposeOct != nullptr) ? juce::roundToInt (rp.transposeOct->load()) : 0;
            psgEngine.noteOffNoise (juce::jlimit (0, 127,
                MidiRouter::applyTranspose (note, st, oc)));
        }
        else if (dest.isDac())
        {
            dacPlayer.release();
        }
    });
}

void GenVstAudioProcessor::handlePitchBend (int channel, int bend14bit)
{
    const double semitones = MidiRouter::pitchBendToSemitones (bend14bit, currentBendRangeSemitones());

    midiRouter.forEachDestination (channel, [&] (MidiRouter::Destination dest)
    {
        if (dest.isFmPart())
        {
            const int part = dest.index;
            midiRouter.setPitchBendSemitones (part, semitones);

            // Reflect the bend into every active voice of this part — the
            // dirty-diff sees only the frequency registers change.
            paramCache.readPatch (part, partPatches[(size_t) part]);
            voiceAllocator.setPitchBend (part, semitones, partPatches[(size_t) part], currentVelToTl());
        }
        else if (dest.isPsgTone())
        {
            psgEngine.setPitchBendSemitones (dest.index, semitones);
        }
        // PSG noise has no pitch; DAC has no bend either.
    });
}

void GenVstAudioProcessor::handleAftertouch (int channel, int pressure)
{
    const int target = currentAftertouchTarget();
    if (target == 0) return;   // Off

    midiRouter.forEachDestination (channel, [&] (MidiRouter::Destination dest)
    {
        if (! dest.isFmPart()) return;
        const int part = dest.index;

        if (target == 1)   // LFO Depth -> PMS (0..7), routed through CC 73
        {
            const int pmsValue = MidiRouter::scaleCC (pressure, 7);
            writeIntParam (ccParamLookup[(size_t) part][73], pmsValue);
            paramCache.readPatch (part, partPatches[(size_t) part]);
            voiceAllocator.updateActiveVoicesForPart (part, partPatches[(size_t) part], currentVelToTl());
        }
        // target == 2 (Carrier TL): the Settings selector exists from Task 13
        // but the TL modulation path is reserved for a later task.
    });
}

void GenVstAudioProcessor::handleProgramChange (int channel, int program)
{
    const Patch* patch = patchBrowser.factoryPatchByIndex (program);
    if (patch == nullptr)
        return;   // out-of-range PC indices silently no-op (MIDI convention)

    midiRouter.forEachDestination (channel, [&] (MidiRouter::Destination dest)
    {
        if (! dest.isFmPart()) return;
        const int part = dest.index;

        // Audio-thread apply via raw atomic stores — same path the
        // delivery-queue drain takes. No allocation, no message-thread bounce;
        // the dirty-diff on the rest of this block routes the new register
        // values into sounding and newly-started voices on `part`. Bypasses
        // setValueNotifyingHost, so the host's automation lane / UI knobs
        // catch up on their next poll (a tiny visual lag, never an audio
        // glitch).
        applyPatchOnAudioThread (part, *patch);
    });
}

void GenVstAudioProcessor::handleControlChange (int channel, int cc, int value)
{
    // Panic + reset apply globally and don't need a routing destination.
    if (cc == 120)   // All Sound Off — immediate, no release
    {
        voiceAllocator.allSoundOff();
        return;
    }
    if (cc == 123)   // All Notes Off — release naturally
    {
        voiceAllocator.allNotesOff();
        psgEngine.reset();
        dacPlayer.release();
        return;
    }
    if (cc == 121)   // Reset All Controllers — per channel
    {
        resetControllersForChannel (channel);
        return;
    }

    // Global PSG / DAC CCs — work on any incoming channel
    // (07-feature-spec.md "MIDI CC Map").
    if (cc == 84)   // DAC enable: 0 = off, >=64 = on
    {
        if (psgDacParams.dacEnable != nullptr)
            psgDacParams.dacEnable->store (value >= 64 ? 1.0f : 0.0f,
                                           std::memory_order_relaxed);
        return;
    }
    if (cc == 85)   // PSG mix level: 0..127 -> 0..1
    {
        if (psgDacParams.mix != nullptr)
            psgDacParams.mix->store (juce::jlimit (0.0f, 1.0f,
                                                   value / 127.0f),
                                     std::memory_order_relaxed);
        return;
    }

    // CC 7 (master volume) has no per-part volume parameter yet, so we
    // accept-and-ignore it rather than rejecting the message.
    if (cc == 7)
        return;

    midiRouter.forEachDestination (channel, [&] (MidiRouter::Destination dest)
    {
        if (! dest.isFmPart()) return;
        const int part = dest.index;

        // Sustain pedal (CC 64): >= 64 = down, < 64 = up. On pedal-up,
        // release every voice this part had deferred during the hold.
        if (cc == 64)
        {
            const bool wasHeld = midiRouter.sustainPedalHeld (part);
            const bool nowHeld = value >= 64;
            midiRouter.setSustainPedalHeld (part, nowHeld);
            if (wasHeld && ! nowHeld)
                voiceAllocator.releaseSustained (part);
            return;
        }

        // Pan (CC 10) — three-zone L/center/R, the YM2612 output-enable bits.
        if (cc == 10)
        {
            int lr;
            if (value <= 63)      lr = 2;   // L only
            else if (value == 64) lr = 3;   // both (center)
            else                  lr = 1;   // R only

            writeIntParam (lrParamLookup[(size_t) part], lr);
            paramCache.readPatch (part, partPatches[(size_t) part]);
            voiceAllocator.updateActiveVoicesForPart (part, partPatches[(size_t) part], currentVelToTl());
            return;
        }

        // The mapped-CC table covers the operator/channel FM parameters.
        // Anything unmapped (mod wheel range, etc. — except those handled
        // above) silently no-ops, matching standard MIDI behavior.
        auto* target = ccParamLookup[(size_t) part][(size_t) cc];
        if (target == nullptr) return;

        const int max = MidiRouter::ccMaxValue (cc);
        if (max <= 0) return;

        const int hardwareValue = MidiRouter::scaleCC (value, max);
        writeIntParam (target, hardwareValue);
        paramCache.readPatch (part, partPatches[(size_t) part]);
        voiceAllocator.updateActiveVoicesForPart (part, partPatches[(size_t) part], currentVelToTl());
    });
}

void GenVstAudioProcessor::resetControllersForChannel (int channel)
{
    midiRouter.forEachDestination (channel, [&] (MidiRouter::Destination dest)
    {
        if (! dest.isFmPart()) return;

        const int part = dest.index;
        midiRouter.resetControllers (part);
        voiceAllocator.releaseSustained (part);

        paramCache.readPatch (part, partPatches[(size_t) part]);
        voiceAllocator.setPitchBend (part, 0.0, partPatches[(size_t) part], currentVelToTl());
    });
}

void GenVstAudioProcessor::writeIntParam (std::atomic<float>* target, int value) noexcept
{
    if (target == nullptr) return;
    // Direct atomic store: the audio thread is the sole reader, the host's
    // automation lane sees the change on its next poll. Bypasses
    // setValueNotifyingHost because that allocates / takes locks — not safe
    // here. UI relays poll periodically, so a transient stale-knob is the
    // worst sync gap.
    target->store (static_cast<float> (value), std::memory_order_relaxed);
}

int GenVstAudioProcessor::currentBendRangeSemitones() const noexcept
{
    static constexpr int kRanges[] { 1, 2, 7, 12 };
    if (bendRangeParam == nullptr) return 2;
    const int idx = juce::jlimit (0, 3, juce::roundToInt (bendRangeParam->load()));
    return kRanges[idx];
}

bool GenVstAudioProcessor::currentVelToTl() const noexcept
{
    return velToTlParam != nullptr && velToTlParam->load() > 0.5f;
}

int GenVstAudioProcessor::currentAftertouchTarget() const noexcept
{
    if (aftertouchTargetParam == nullptr) return 1;
    return juce::jlimit (0, 2, juce::roundToInt (aftertouchTargetParam->load()));
}

// --- Task 22 — Rack routing helpers ------------------------------------------

int GenVstAudioProcessor::fmPartTransposedNote (int part, int noteIn) const noexcept
{
    if (part < 0 || part >= PartManager::kNumParts) return noteIn;
    const auto& p = fmRackParams[(std::size_t) part];
    const int st = (p.transposeSt  != nullptr) ? juce::roundToInt (p.transposeSt->load())  : 0;
    const int oc = (p.transposeOct != nullptr) ? juce::roundToInt (p.transposeOct->load()) : 0;
    return MidiRouter::applyTranspose (noteIn, st, oc);
}

bool GenVstAudioProcessor::fmPartAcceptsNote (int part, int transposedNote) const noexcept
{
    if (part < 0 || part >= PartManager::kNumParts) return true;
    const auto& p = fmRackParams[(std::size_t) part];
    const int lo = (p.noteLo != nullptr) ? juce::roundToInt (p.noteLo->load()) : 0;
    const int hi = (p.noteHi != nullptr) ? juce::roundToInt (p.noteHi->load()) : 127;
    return MidiRouter::noteInRange (transposedNote, lo, hi);
}

double GenVstAudioProcessor::fmPartDetuneSemitones (int part) const noexcept
{
    if (part < 0 || part >= PartManager::kNumParts) return 0.0;
    const auto& p = fmRackParams[(std::size_t) part];
    if (p.detuneCents == nullptr) return 0.0;
    return MidiRouter::detuneCentsToSemitones (juce::roundToInt (p.detuneCents->load()));
}

int GenVstAudioProcessor::fmPartMidiChannel (int part) const noexcept
{
    if (part < 0 || part >= PartManager::kNumParts) return part + 1;
    const auto& p = fmRackParams[(std::size_t) part];
    if (p.midiCh == nullptr) return part + 1;
    return juce::jlimit (0, 16, juce::roundToInt (p.midiCh->load()));
}

int GenVstAudioProcessor::psgChannelMidiChannel (int psgCh) const noexcept
{
    if (psgCh < 0 || psgCh >= SN76489Engine::kNumChannels)
        return 0;
    const auto& p = psgRackParams[(std::size_t) psgCh];
    if (p.midiCh == nullptr) return 0;
    return juce::jlimit (0, 16, juce::roundToInt (p.midiCh->load()));
}

int GenVstAudioProcessor::dacMidiChannel() const noexcept
{
    if (dacRackParams.midiCh == nullptr) return 16;
    return juce::jlimit (0, 16, juce::roundToInt (dacRackParams.midiCh->load()));
}

void GenVstAudioProcessor::syncRackRoutingToTable() noexcept
{
    // Push every cached rack midi-channel into the MidiRouter destination
    // table. Audio-thread safe — setDestinationChannelDeferred only writes
    // the destChannel atomic; the consolidated rebuildChannelMask runs once
    // at the end. The audio thread reads the destChannel atomics on the next
    // forEachDestination call.
    bool anyChanged = false;
    auto sync = [&] (MidiRouter::Destination dest, int channel)
    {
        const int destId = MidiRouter::destinationId (dest);
        if (destId < 0) return;
        if (midiRouter.destinationChannel (destId) != channel)
        {
            midiRouter.setDestinationChannelDeferred (destId, channel);
            anyChanged = true;
        }
    };
    for (int part = 0; part < PartManager::kNumParts; ++part)
        sync ({ MidiRouter::Destination::Kind::FmPart, part },
              fmPartMidiChannel (part));
    for (int t = 0; t < 3; ++t)
        sync ({ MidiRouter::Destination::Kind::PsgTone, t },
              psgChannelMidiChannel (t));
    sync ({ MidiRouter::Destination::Kind::PsgNoise, 0 },
          psgChannelMidiChannel (SN76489Engine::kNoiseCh));
    sync ({ MidiRouter::Destination::Kind::Dac, 0 }, dacMidiChannel());

    if (anyChanged)
        midiRouter.rebuildChannelMaskAfterDeferredWrites();
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
    // Full Task-16 state: apvts + per-part patch paths & MIDI channels +
    // routing table + DAC PCM (base64) + custom roots. See PluginState.h.
    if (auto xml = genvst::state::save (*this))
        copyXmlToBinary (*xml, destData);
}

void GenVstAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr) return;

    // PluginState parses the format (legacy bare-apvts vs Task-16 wrapper),
    // restores apvts immediately and either applies or defers the patch
    // browser-dependent steps. Unresolved patch / custom-root paths are
    // enqueued via addPendingNotification — the editor's timer drains them.
    genvst::state::restore (*this, *xml);

    // Tell prepareToPlay's first-run path to skip the dev-patch fallback,
    // so the restored patches are not overwritten the next time the host
    // re-prepares the plugin.
    stateRestored = true;
}

void GenVstAudioProcessor::addPendingNotification (juce::String level, juce::String message)
{
    const std::lock_guard<std::mutex> lk (pendingNotificationsMutex);
    pendingNotifications.push_back ({ std::move (level), std::move (message) });
}

void GenVstAudioProcessor::queuePreviewNoteOn (int part, int note, int velocity) noexcept
{
    if (part < 0 || part >= PartManager::kNumParts) return;
    if (velocity <= 0) velocity = 1;   // a real note-on must have vel > 0

    int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
    previewFifo.prepareToWrite (1, start1, size1, start2, size2);
    if (size1 + size2 < 1) return;     // full -> drop (Preview is non-critical)

    const int slot = size1 > 0 ? start1 : start2;
    previewSlots[(std::size_t) slot] = { part, note, velocity };
    previewFifo.finishedWrite (1);
}

void GenVstAudioProcessor::queuePreviewNoteOff (int part, int note) noexcept
{
    if (part < 0 || part >= PartManager::kNumParts) return;

    int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
    previewFifo.prepareToWrite (1, start1, size1, start2, size2);
    if (size1 + size2 < 1) return;

    const int slot = size1 > 0 ? start1 : start2;
    previewSlots[(std::size_t) slot] = { part, note, 0 };
    previewFifo.finishedWrite (1);
}

void GenVstAudioProcessor::drainPreviewQueue()
{
    int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
    const int ready = previewFifo.getNumReady();
    if (ready <= 0) return;
    previewFifo.prepareToRead (ready, start1, size1, start2, size2);

    auto handle = [this] (const PreviewEvent& ev)
    {
        if (ev.velocity > 0)
        {
            paramCache.readPatch (ev.part, noteOnPatch);
            voiceAllocator.noteOn (ev.part, ev.note, ev.velocity,
                                   midiRouter.pitchBendSemitones (ev.part),
                                   currentVelToTl(), noteOnPatch);
        }
        else
        {
            voiceAllocator.noteOff (ev.part, ev.note,
                                    midiRouter.sustainPedalHeld (ev.part));
        }
    };

    for (int i = 0; i < size1; ++i) handle (previewSlots[(std::size_t) (start1 + i)]);
    for (int i = 0; i < size2; ++i) handle (previewSlots[(std::size_t) (start2 + i)]);
    previewFifo.finishedRead (size1 + size2);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new GenVstAudioProcessor();
}
