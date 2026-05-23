#include "MidiRouter.h"

#include <algorithm>
#include <cmath>

namespace
{
    // CC -> apvts param descriptor. `op == -1` is a per-part parameter; op
    // 0..3 picks a per-operator parameter. `maxValue` matches the hardware
    // range from 07-feature-spec.md "MIDI CC Map".
    struct CcMapEntry
    {
        const char* base   = nullptr;   // e.g. "tl", "alg"; nullptr => unmapped
        int         op     = -1;        // -1 = per-part, 0..3 = per-operator
        int         maxVal = 0;         // 0 also means unmapped
    };

    // The full CC table, indexed by CC number (0..127). Empty entries are
    // unmapped or handled specially (CC 64 sustain, CC 120/121/123 panic, etc.).
    constexpr CcMapEntry buildCcMap (int cc)
    {
        switch (cc)
        {
            case 1:  return { "pms", -1, 7 };    // mod wheel -> vibrato (PMS)
            // CC 7 (master volume) and CC 10 (pan) hit special-case paths in
            // MidiRouter::ccToParamId — they don't map 1:1 to a single
            // numeric apvts param (no per-part volume yet; LR is a 2-bit field).
            case 14: return { "alg", -1, 7 };
            case 15: return { "fb",  -1, 7 };

            case 16: return { "tl", 0, 127 };
            case 17: return { "tl", 1, 127 };
            case 18: return { "tl", 2, 127 };
            case 19: return { "tl", 3, 127 };

            case 20: return { "mul", 0, 15 };
            case 21: return { "mul", 1, 15 };
            case 22: return { "mul", 2, 15 };
            case 23: return { "mul", 3, 15 };

            case 24: return { "dt", 0, 6 };
            case 25: return { "dt", 1, 6 };
            case 26: return { "dt", 2, 6 };
            case 27: return { "dt", 3, 6 };

            case 28: return { "ar", 0, 31 };
            case 29: return { "ar", 1, 31 };
            case 30: return { "ar", 2, 31 };
            case 31: return { "ar", 3, 31 };

            case 32: return { "dr", 0, 31 };
            case 33: return { "dr", 1, 31 };
            case 34: return { "dr", 2, 31 };
            case 35: return { "dr", 3, 31 };

            case 36: return { "sr", 0, 31 };
            case 37: return { "sr", 1, 31 };
            case 38: return { "sr", 2, 31 };
            case 39: return { "sr", 3, 31 };

            case 40: return { "rr", 0, 15 };
            case 41: return { "rr", 1, 15 };
            case 42: return { "rr", 2, 15 };
            case 43: return { "rr", 3, 15 };

            case 44: return { "sl", 0, 15 };
            case 45: return { "sl", 1, 15 };
            case 46: return { "sl", 2, 15 };
            case 47: return { "sl", 3, 15 };

            case 48: return { "ks", 0, 3 };
            case 49: return { "ks", 1, 3 };
            case 50: return { "ks", 2, 3 };
            case 51: return { "ks", 3, 3 };

            case 70: return { "lfo_enable", -1, 1 };
            case 71: return { "lfo_rate",   -1, 7 };
            case 72: return { "ams",        -1, 3 };
            case 73: return { "pms",        -1, 7 };

            case 80: return { "amon", 0, 1 };
            case 81: return { "amon", 1, 1 };
            case 82: return { "amon", 2, 1 };
            case 83: return { "amon", 3, 1 };

            // CC 84/85 (DAC enable / PSG mix) are Task 07.
            default: return {};
        }
    }
}

int MidiRouter::scaleCC (int ccValue, int maxValue) noexcept
{
    const int v = std::clamp (ccValue, 0, 127);
    const int scaled = static_cast<int> (std::lround (v * static_cast<double> (maxValue) / 127.0));
    return std::clamp (scaled, 0, maxValue);
}

double MidiRouter::pitchBendToSemitones (int bend14bit, int rangeSemitones) noexcept
{
    const int centred = std::clamp (bend14bit, 0, 16383) - 8192;   // -8192..8191
    return centred / 8192.0 * static_cast<double> (rangeSemitones);
}

std::optional<juce::String> MidiRouter::ccToParamId (int ccNumber, int part)
{
    if (part < 0 || part >= PartManager::kNumParts)
        return std::nullopt;
    if (ccNumber < 0 || ccNumber > 127)
        return std::nullopt;

    const CcMapEntry e = buildCcMap (ccNumber);
    if (e.base == nullptr || e.maxVal == 0)
        return std::nullopt;

    juce::String id (e.base);
    if (e.op >= 0)
        id += "_op" + juce::String (e.op + 1);
    id += "_part" + juce::String (part + 1);
    return id;
}

int MidiRouter::ccMaxValue (int ccNumber) noexcept
{
    if (ccNumber < 0 || ccNumber > 127)
        return -1;
    const CcMapEntry e = buildCcMap (ccNumber);
    return (e.base != nullptr && e.maxVal > 0) ? e.maxVal : -1;
}

MidiRouter::MidiRouter()
{
    resetRouting();
}

void MidiRouter::resetRouting() noexcept
{
    for (auto& d : channelMap)
        d = { Destination::Kind::None, -1 };

    // Default binding: MIDI channel i+1 -> FM part i for i = 0..5.
    for (int part = 0; part < PartManager::kNumParts; ++part)
        channelMap[static_cast<std::size_t> (part + 1)] =
            { Destination::Kind::FmPart, part };

    bendSemitones.fill (0.0);
    sustainHeld.fill (false);
}

void MidiRouter::setDestination (int midiChannel, Destination dest) noexcept
{
    if (midiChannel < 1 || midiChannel > 16)
        return;
    channelMap[static_cast<std::size_t> (midiChannel)] = dest;
}

MidiRouter::Destination MidiRouter::destinationFor (int midiChannel) const noexcept
{
    if (midiChannel < 1 || midiChannel > 16)
        return {};
    return channelMap[static_cast<std::size_t> (midiChannel)];
}

double MidiRouter::pitchBendSemitones (int part) const noexcept
{
    if (part < 0 || part >= PartManager::kNumParts) return 0.0;
    return bendSemitones[static_cast<std::size_t> (part)];
}

void MidiRouter::setPitchBendSemitones (int part, double semitones) noexcept
{
    if (part < 0 || part >= PartManager::kNumParts) return;
    bendSemitones[static_cast<std::size_t> (part)] = semitones;
}

bool MidiRouter::sustainPedalHeld (int part) const noexcept
{
    if (part < 0 || part >= PartManager::kNumParts) return false;
    return sustainHeld[static_cast<std::size_t> (part)];
}

void MidiRouter::setSustainPedalHeld (int part, bool held) noexcept
{
    if (part < 0 || part >= PartManager::kNumParts) return;
    sustainHeld[static_cast<std::size_t> (part)] = held;
}

void MidiRouter::resetControllers (int part) noexcept
{
    if (part < 0 || part >= PartManager::kNumParts) return;
    bendSemitones[static_cast<std::size_t> (part)] = 0.0;
    sustainHeld  [static_cast<std::size_t> (part)] = false;
}
