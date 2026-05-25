#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>

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
            layout.add (std::make_unique<juce::AudioParameterInt> (
                juce::ParameterID { opParamId (d.id, op), 1 },
                displayName (d.id, op),
                d.lo, d.hi, d.lo));

    // FM v2 additions — 04-patch-system.md *Defaults on legacy-format load*.
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "freq_ctrl_mode", 1 }, "Freq Ctrl Mode",
        juce::StringArray { "INT_MUL", "FLOAT_MUL", "FIXED_HZ" }, 0));
    layout.add (std::make_unique<juce::AudioParameterInt> (
        juce::ParameterID { "retrig_rate", 1 }, "Retrig Rate", 0, 1023, 500));

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

    return layout;
}

void FmParamCache::connect (juce::AudioProcessorValueTreeState& apvts)
{
    for (int d = 0; d < kNumOpParams; ++d)
        for (int op = 0; op < kNumOps; ++op)
            opParam[d][op] = apvts.getRawParameterValue (opParamId (kOpParams[d].id, op));

    for (int d = 0; d < kNumPartParams; ++d)
        partParam[d] = apvts.getRawParameterValue (juce::String (kPartParams[d].id));
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
    patchBrowser.drainAudioThreadQueue (
        [this] (int /* part */, const Patch& p)
        {
            for (int d = 0; d < FmParamCache::kNumOpParams; ++d)
                for (int op = 0; op < FmParamCache::kNumOps; ++op)
                    if (auto* ptr = paramCache.opParam[d][op])
                        ptr->store (static_cast<float> ((p.*(kOpParams[d].field))[op]),
                                    std::memory_order_relaxed);
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

    // Snapshot the current single-engine patch and propagate to active voices.
    paramCache.readPatch (currentPatch);
    voiceAllocator.updateActiveVoicesForPart (0, currentPatch, /*velToTl*/ true);

    const int numChannels = buffer.getNumChannels();
    float* left  = buffer.getWritePointer (0) + startSample;
    float* right = numChannels > 1 ? buffer.getWritePointer (1) + startSample
                                   : monoScratch.get();

    // Clear destinations (the engines render-add into them).
    std::fill_n (left, numSamples, 0.0f);
    if (numChannels > 1) std::fill_n (right, numSamples, 0.0f);

    voiceAllocator.render (left, right, numSamples);
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
            paramCache.readPatch (currentPatch);
            voiceAllocator.noteOn (0, note, velocity, /*bend*/ 0.0,
                                   /*velToTl*/ true, currentPatch);
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
        voiceAllocator.setPitchBend (0, semitones, currentPatch, /*velToTl*/ true);
    }
    else if (mode == Mode::SQ)
    {
        for (int t = 0; t < kPsgTones; ++t)
            psgEngine.setPitchBendSemitones (t, semitones);
    }
}

void GenVstAudioProcessor::handleControlChange (int cc, int value)
{
    // CC 1 = Modulation Wheel — mirror the live value into the display-only
    // apvts param so the GLOBAL IN wheel widget tracks it.
    if (cc == 1)
    {
        if (modWheelMirrorParam != nullptr)
            modWheelMirrorParam->store (juce::jlimit (0.0f, 1.0f, value / 127.0f),
                                        std::memory_order_relaxed);
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
    // Full CC → param dispatch is reconnected in Task 05 (FM) / Task 06 (SQ).
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
