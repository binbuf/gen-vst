#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>

#include "PatchSystem.h"

namespace
{
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
    masterGainParam = apvts.getRawParameterValue ("master_gain");
    loadDevPatches();
}

void GenVstAudioProcessor::applyPatchToPart (int part, const Patch& patch)
{
    partManager.loadPatch (part, patch);
    writePatchToParams (apvts, part, patch);
}

void GenVstAudioProcessor::loadDevPatches()
{
    // Dev wiring: load two distinct factory patches so the plugin sounds — and
    // demonstrably plays multitimbral — before the patch browser exists.
    // organ.tfi fills every part; bass.tfi overrides part 1, so notes on MIDI
    // channels 1 and 2 play two different timbres. Superseded by Task 09.
#ifdef GENVST_DEV_PATCH_DIR
    const std::filesystem::path dir { GENVST_DEV_PATCH_DIR };

    if (auto organ = loadTFI (dir / "organ.tfi"); organ.patch.has_value())
        for (int part = 0; part < PartManager::kNumParts; ++part)
            applyPatchToPart (part, *organ.patch);

    if (auto bass = loadTFI (dir / "bass.tfi"); bass.patch.has_value())
        applyPatchToPart (1, *bass.patch);
#endif
}

void GenVstAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    // Build the raw-pointer parameter cache (per part) and the voice pool.
    paramCache.connect (apvts);
    voiceAllocator.prepare (sampleRate, samplesPerBlock);
    monoScratch.allocate ((size_t) juce::jmax (1, samplesPerBlock), true);
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

    // Block-granular MIDI: every event is applied at the block boundary, then
    // the whole block is rendered. Sample-accurate splitting is Task 06.
    for (const auto metadata : midiMessages)
    {
        const auto msg = metadata.getMessage();

        if (msg.isNoteOn())
        {
            const int part = partManager.partForMidiChannel (msg.getChannel());
            if (part >= 0)
            {
                paramCache.readPatch (part, noteOnPatch);
                voiceAllocator.noteOn (part, msg.getNoteNumber(),
                                       msg.getVelocity(), noteOnPatch);
            }
        }
        else if (msg.isNoteOff())
        {
            const int part = partManager.partForMidiChannel (msg.getChannel());
            if (part >= 0)
                voiceAllocator.noteOff (part, msg.getNoteNumber());
        }
        else if (msg.isAllNotesOff())
        {
            voiceAllocator.allNotesOff();
        }
        else if (msg.isAllSoundOff())
        {
            voiceAllocator.allSoundOff();
        }
    }

    if (numChannels == 0 || numSamples == 0)
        return;

    // Dirty-diff: snapshot every part's current parameters, then write only the
    // changed registers to each sounding voice, so live edits/automation reach
    // held notes without a retrigger.
    for (int part = 0; part < PartManager::kNumParts; ++part)
        paramCache.readPatch (part, partPatches[(size_t) part]);
    voiceAllocator.updateActiveVoices (partPatches);

    float* left  = buffer.getWritePointer (0);
    float* right = numChannels > 1 ? buffer.getWritePointer (1) : monoScratch.get();

    voiceAllocator.render (left, right, numSamples);

    const float gain = masterGainParam->load();

    for (int i = 0; i < numSamples; ++i)
        left[i] = softClip (left[i] * gain);

    if (numChannels > 1)
        for (int i = 0; i < numSamples; ++i)
            right[i] = softClip (right[i] * gain);

    // Silence any output channels beyond the stereo pair.
    for (int ch = 2; ch < numChannels; ++ch)
        buffer.clear (ch, 0, numSamples);
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
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void GenVstAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new GenVstAudioProcessor();
}
