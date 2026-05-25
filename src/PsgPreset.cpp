#include "PsgPreset.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <sstream>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_core/juce_core.h>

namespace
{
    template <typename T>
    T clampVal (T v, T lo, T hi) noexcept
    {
        return std::clamp (v, lo, hi);
    }

    int readInt (const juce::var& obj, const char* key, int fallback) noexcept
    {
        if (! obj.isObject()) return fallback;
        const auto* dyn = obj.getDynamicObject();
        if (dyn == nullptr) return fallback;
        const auto v = dyn->getProperty (juce::Identifier (key));
        if (v.isInt() || v.isDouble() || v.isInt64()) return static_cast<int> (v);
        return fallback;
    }

    float readFloat (const juce::var& obj, const char* key, float fallback) noexcept
    {
        if (! obj.isObject()) return fallback;
        const auto* dyn = obj.getDynamicObject();
        if (dyn == nullptr) return fallback;
        const auto v = dyn->getProperty (juce::Identifier (key));
        if (v.isDouble() || v.isInt() || v.isInt64()) return static_cast<float> ((double) v);
        return fallback;
    }

    std::string readStringField (const juce::var& obj, const char* key, const char* fallback)
    {
        if (! obj.isObject()) return fallback;
        const auto* dyn = obj.getDynamicObject();
        if (dyn == nullptr) return fallback;
        const auto v = dyn->getProperty (juce::Identifier (key));
        if (v.isString()) return v.toString().toStdString();
        return fallback;
    }

    juce::var getChild (const juce::var& parent, const char* key) noexcept
    {
        if (! parent.isObject()) return {};
        const auto* dyn = parent.getDynamicObject();
        if (dyn == nullptr) return {};
        return dyn->getProperty (juce::Identifier (key));
    }

    PsgPresetChannel parseToneChannel (const juce::var& obj)
    {
        PsgPresetChannel c {};
        c.atk    = clampVal (readInt   (obj, "atk",     0),     0,   31);
        c.dr1    = clampVal (readInt   (obj, "dr1",     0),     0,   31);
        c.sus    = clampVal (readInt   (obj, "sus",     0),     0,   15);
        c.dr2    = clampVal (readInt   (obj, "dr2",     0),     0,   31);
        c.rr     = clampVal (readInt   (obj, "rr",      0),     0,   15);
        c.vol    = clampVal (readFloat (obj, "vol",  1.0f),   0.0f, 1.0f);
        c.pan    = clampVal (readFloat (obj, "pan",  0.0f),  -1.0f, 1.0f);
        c.detune = clampVal (readInt   (obj, "detune",  0),  -100,  100);
        return c;
    }

    PsgPresetChannel parseNoiseChannel (const juce::var& obj)
    {
        PsgPresetChannel c {};
        c.atk = clampVal (readInt   (obj, "atk",   0),    0,   31);
        c.dr1 = clampVal (readInt   (obj, "dr1",   0),    0,   31);
        c.sus = clampVal (readInt   (obj, "sus",   0),    0,   15);
        c.dr2 = clampVal (readInt   (obj, "dr2",   0),    0,   31);
        c.rr  = clampVal (readInt   (obj, "rr",    0),    0,   15);
        c.vol = clampVal (readFloat (obj, "vol",1.0f),  0.0f, 1.0f);
        c.pan = clampVal (readFloat (obj, "pan",0.0f), -1.0f, 1.0f);
        c.detune = 0;
        return c;
    }

    // Noise-type / -rate: clamp unknown strings to the apvts choice defaults
    // ("white" / "mid") so a typo in the JSON still loads.
    std::string clampNoiseType (const std::string& s)
    {
        return (s == "white" || s == "periodic") ? s : std::string ("white");
    }

    std::string clampNoiseRate (const std::string& s)
    {
        return (s == "low" || s == "mid" || s == "high" || s == "ch2")
                 ? s
                 : std::string ("mid");
    }

    juce::var toneChannelJson (const PsgPresetChannel& c)
    {
        auto* obj = new juce::DynamicObject();
        obj->setProperty ("atk",    c.atk);
        obj->setProperty ("dr1",    c.dr1);
        obj->setProperty ("sus",    c.sus);
        obj->setProperty ("dr2",    c.dr2);
        obj->setProperty ("rr",     c.rr);
        obj->setProperty ("vol",    (double) c.vol);
        obj->setProperty ("pan",    (double) c.pan);
        obj->setProperty ("detune", c.detune);
        return juce::var (obj);
    }

    juce::var noiseChannelJson (const PsgPresetChannel& c,
                                 const std::string& type,
                                 const std::string& rate)
    {
        auto* obj = new juce::DynamicObject();
        obj->setProperty ("atk",  c.atk);
        obj->setProperty ("dr1",  c.dr1);
        obj->setProperty ("sus",  c.sus);
        obj->setProperty ("dr2",  c.dr2);
        obj->setProperty ("rr",   c.rr);
        obj->setProperty ("vol",  (double) c.vol);
        obj->setProperty ("pan",  (double) c.pan);
        obj->setProperty ("type", juce::String (type));
        obj->setProperty ("rate", juce::String (rate));
        return juce::var (obj);
    }

    // Convert a clamped value into the apvts normalised [0,1] range using the
    // parameter's own NormalisableRange — handles skewed / non-default ranges
    // correctly (e.g. detune is -100..+100).
    void setParamScaled (juce::AudioProcessorValueTreeState& apvts,
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

    void setParamChoice (juce::AudioProcessorValueTreeState& apvts,
                         const juce::String& id, int choiceIndex)
    {
        if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter (id)))
        {
            const int last = juce::jmax (0, p->choices.size() - 1);
            const int idx  = juce::jlimit (0, last, choiceIndex);
            const float n  = last == 0 ? 0.0f : static_cast<float> (idx) / static_cast<float> (last);
            p->beginChangeGesture();
            p->setValueNotifyingHost (n);
            p->endChangeGesture();
        }
    }

    int noiseTypeIndex (const std::string& s)
    {
        return s == "periodic" ? 1 : 0;
    }

    int noiseRateIndex (const std::string& s)
    {
        if (s == "low")  return 0;
        if (s == "high") return 2;
        if (s == "ch2")  return 3;
        return 1;   // "mid" default
    }

    std::string noiseTypeFromIndex (int idx)
    {
        return idx == 1 ? "periodic" : "white";
    }

    std::string noiseRateFromIndex (int idx)
    {
        switch (idx)
        {
            case 0: return "low";
            case 2: return "high";
            case 3: return "ch2";
            default: return "mid";
        }
    }

    // Suffix mapping that mirrors the apvts param IDs declared in
    // PluginProcessor::createParameterLayout().
    constexpr const char* kToneIds[]  { "ch1", "ch2", "ch3" };
    constexpr const char* kNoiseId      = "noise";

    void applyChannel (juce::AudioProcessorValueTreeState& apvts,
                       const char* suffixId,
                       const PsgPresetChannel& c)
    {
        const juce::String s = juce::String ("_") + suffixId;
        setParamScaled (apvts, "psg_atk" + s, (float) c.atk);
        setParamScaled (apvts, "psg_dr1" + s, (float) c.dr1);
        setParamScaled (apvts, "psg_sus" + s, (float) c.sus);
        setParamScaled (apvts, "psg_dr2" + s, (float) c.dr2);
        setParamScaled (apvts, "psg_rr"  + s, (float) c.rr);
        setParamScaled (apvts, "psg_vol" + s, c.vol);
        setParamScaled (apvts, "psg_pan" + s, c.pan);
    }

    int readApvtsInt (const juce::AudioProcessorValueTreeState& apvts, const juce::String& id, int fb)
    {
        if (const auto* p = apvts.getRawParameterValue (id))
            return juce::roundToInt (p->load());
        return fb;
    }

    float readApvtsFloat (const juce::AudioProcessorValueTreeState& apvts, const juce::String& id, float fb)
    {
        if (const auto* p = apvts.getRawParameterValue (id))
            return p->load();
        return fb;
    }

    PsgPresetChannel readChannelFromApvts (const juce::AudioProcessorValueTreeState& apvts,
                                           const char* suffixId, bool isNoise)
    {
        const juce::String s = juce::String ("_") + suffixId;
        PsgPresetChannel c {};
        c.atk = readApvtsInt   (apvts, "psg_atk" + s, 0);
        c.dr1 = readApvtsInt   (apvts, "psg_dr1" + s, 0);
        c.sus = readApvtsInt   (apvts, "psg_sus" + s, 0);
        c.dr2 = readApvtsInt   (apvts, "psg_dr2" + s, 0);
        c.rr  = readApvtsInt   (apvts, "psg_rr"  + s, 0);
        c.vol = readApvtsFloat (apvts, "psg_vol" + s, 1.0f);
        c.pan = readApvtsFloat (apvts, "psg_pan" + s, 0.0f);
        c.detune = isNoise ? 0 : readApvtsInt (apvts, "psg_detune" + s, 0);
        return c;
    }
}

PsgPresetLoadResult loadPsgPreset (const std::filesystem::path& path)
{
    std::ifstream file (path);
    if (! file)
        return { std::nullopt, "cannot open file: " + path.string() };

    std::stringstream buffer;
    buffer << file.rdbuf();
    const std::string text = buffer.str();

    juce::var parsed;
    const auto result = juce::JSON::parse (juce::String (text), parsed);
    if (result.failed() || ! parsed.isObject())
        return { std::nullopt,
                 "invalid .psg JSON: "
                     + (result.failed() ? result.getErrorMessage().toStdString()
                                        : std::string ("not an object")) };

    const int version = readInt (parsed, "version", 1);
    if (version != 1)
        return { std::nullopt,
                 ".psg version " + std::to_string (version)
                     + " not supported (only version 1 is accepted)" };

    PsgPreset out;
    out.version = 1;
    out.name    = readStringField (parsed, "name", path.stem().string().c_str());

    const auto channels = getChild (parsed, "channels");
    if (channels.isObject())
    {
        const char* toneKeys[PsgPreset::kNumTones] { "tone1", "tone2", "tone3" };
        for (int i = 0; i < PsgPreset::kNumTones; ++i)
            out.tones[(std::size_t) i] = parseToneChannel (getChild (channels, toneKeys[i]));

        const auto noiseObj = getChild (channels, "noise");
        out.noise = parseNoiseChannel (noiseObj);
        out.noiseType = clampNoiseType (readStringField (noiseObj, "type", "white"));
        out.noiseRate = clampNoiseRate (readStringField (noiseObj, "rate", "mid"));
    }

    if (out.name.empty())
        out.name = path.stem().string();

    return { std::move (out), {} };
}

std::string savePsgPreset (const PsgPreset& preset, const std::filesystem::path& path)
{
    auto* channels = new juce::DynamicObject();
    const char* toneKeys[PsgPreset::kNumTones] { "tone1", "tone2", "tone3" };
    for (int i = 0; i < PsgPreset::kNumTones; ++i)
        channels->setProperty (toneKeys[i],
                               toneChannelJson (preset.tones[(std::size_t) i]));

    channels->setProperty ("noise",
                           noiseChannelJson (preset.noise, preset.noiseType,
                                             preset.noiseRate));

    auto* root = new juce::DynamicObject();
    root->setProperty ("version",  preset.version);
    root->setProperty ("name",     juce::String (preset.name));
    root->setProperty ("channels", juce::var (channels));

    const juce::String json = juce::JSON::toString (juce::var (root), /* allOnOneLine */ false);

    std::ofstream file (path, std::ios::binary | std::ios::trunc);
    if (! file)
        return "cannot open file for write: " + path.string();
    file << json.toStdString();
    if (! file)
        return "write failed: " + path.string();
    return {};
}

void applyPsgPresetToApvts (const PsgPreset& preset,
                            juce::AudioProcessorValueTreeState& apvts)
{
    for (int i = 0; i < PsgPreset::kNumTones; ++i)
    {
        const auto& tone = preset.tones[(std::size_t) i];
        applyChannel (apvts, kToneIds[i], tone);
        const juce::String s = juce::String ("_") + kToneIds[i];
        setParamScaled (apvts, "psg_detune" + s, (float) tone.detune);
    }

    applyChannel (apvts, kNoiseId, preset.noise);

    setParamChoice (apvts, "psg_noise_type", noiseTypeIndex (preset.noiseType));
    setParamChoice (apvts, "psg_noise_rate", noiseRateIndex (preset.noiseRate));
}

PsgPreset readPsgPresetFromApvts (const juce::AudioProcessorValueTreeState& apvts,
                                  const std::string& name)
{
    PsgPreset out;
    out.version = 1;
    out.name    = name;

    for (int i = 0; i < PsgPreset::kNumTones; ++i)
        out.tones[(std::size_t) i] = readChannelFromApvts (apvts, kToneIds[i], false);

    out.noise = readChannelFromApvts (apvts, kNoiseId, true);

    if (const auto* p = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter ("psg_noise_type")))
        out.noiseType = noiseTypeFromIndex (p->getIndex());
    if (const auto* p = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter ("psg_noise_rate")))
        out.noiseRate = noiseRateFromIndex (p->getIndex());

    return out;
}

// =============================================================================
// DMP PSG import (ADR-0026)
// =============================================================================

namespace
{
    // DMP v11 header bytes the PSG loader accepts. Mirrors the constants in
    // PatchSystem.cpp's FM DMP loader; copied here so PsgPreset stays
    // independent of PatchSystem's internal-namespace constants.
    constexpr std::uint8_t kDmpVersion       = 0x0B;
    constexpr std::uint8_t kDmpSysGenesis    = 0x02;
    constexpr std::uint8_t kDmpSysGenesisExt = 0x42;
    constexpr std::uint8_t kDmpModePsg       = 0x00;

    // A DMP volume macro slot is a 4-byte little-endian signed int (Furnace's
    // SafeReader::readI). On Genesis PSG the values are bounded to 0..15 by
    // DefleMask itself, but a malformed file could put anything in here; we
    // clamp to the SN76489 attenuation range on read.
    constexpr std::size_t kMacroSlotBytes = 4;

    struct ByteCursor
    {
        const std::vector<std::uint8_t>* bytes = nullptr;
        std::size_t                       pos   = 0;
        bool                              ok    = true;

        bool readByte (std::uint8_t& out)
        {
            if (pos + 1 > bytes->size()) { ok = false; return false; }
            out = (*bytes)[pos++];
            return true;
        }

        bool readI32 (std::int32_t& out)
        {
            if (pos + kMacroSlotBytes > bytes->size()) { ok = false; return false; }
            std::uint32_t v = 0;
            // Little-endian decode — Furnace's host-endian readI on x86 / ARM.
            v |= static_cast<std::uint32_t> ((*bytes)[pos + 0]);
            v |= static_cast<std::uint32_t> ((*bytes)[pos + 1]) << 8;
            v |= static_cast<std::uint32_t> ((*bytes)[pos + 2]) << 16;
            v |= static_cast<std::uint32_t> ((*bytes)[pos + 3]) << 24;
            pos += kMacroSlotBytes;
            std::memcpy (&out, &v, sizeof (out));
            return true;
        }

        bool skip (std::size_t n)
        {
            if (pos + n > bytes->size()) { ok = false; return false; }
            pos += n;
            return true;
        }
    };

    // Read one DMP STD macro (len: signed-char count, len * 4 bytes of
    // little-endian int values, then a 1-byte loop point if len > 0). On
    // success appends the values to `out`. Returns false (and sets cursor.ok
    // = false) on truncation.
    bool readDmpStdMacro (ByteCursor& cur, std::vector<std::int32_t>& out)
    {
        std::uint8_t lenByte = 0;
        if (! cur.readByte (lenByte))
            return false;
        const auto len = static_cast<int> (static_cast<std::int8_t> (lenByte));
        if (len < 0)
            return false;
        out.resize (static_cast<std::size_t> (len));
        for (int i = 0; i < len; ++i)
            if (! cur.readI32 (out[(std::size_t) i]))
                return false;
        if (len > 0)
        {
            std::uint8_t loop = 0;
            if (! cur.readByte (loop))
                return false;
        }
        return true;
    }

    // Skip a DMP STD macro without retaining its values. Used for duty / wave
    // macros that the import bridge does not consume.
    bool skipDmpStdMacro (ByteCursor& cur)
    {
        std::vector<std::int32_t> discard;
        return readDmpStdMacro (cur, discard);
    }

    // Macro → ADSR approximation per ADR-0026 *Macro → ADSR approximation*.
    // `volMacro` carries DefleMask attenuation values (0 = loudest,
    // 15 = silent). `tone` is populated in-place with atk / dr1 / sus / dr2 /
    // rr in the PsgPresetChannel ranges (0..31 / 0..31 / 0..15 / 0..31 /
    // 0..15) so the values land cleanly in the SN76489Engine PsgEnvelope
    // (see SN76489Engine::PsgEnvelope::setRates ranges).
    void approximateAdsr (const std::vector<std::int32_t>& volMacro,
                          PsgPresetChannel&                tone)
    {
        if (volMacro.empty())
        {
            // No macro at all: leave a sensible default ADSR (fast attack,
            // full sustain, short release). Mirrors the factory default.psg.
            tone.atk = 0;
            tone.dr1 = 0;
            tone.sus = 0;
            tone.dr2 = 0;
            tone.rr  = 4;
            return;
        }

        // Clamp each macro value into 0..15. Out-of-range values (e.g., from
        // a chip where DMP uses a wider attenuation range) are saturated.
        std::vector<int> atten (volMacro.size());
        for (std::size_t i = 0; i < volMacro.size(); ++i)
            atten[i] = std::clamp (static_cast<int> (volMacro[i]), 0, 15);

        // Peak loudness = minimum attenuation, taking the FIRST occurrence
        // so a noisy plateau after the peak doesn't shift the attack index.
        int peakIdx   = 0;
        int peakAtten = atten[0];
        for (std::size_t i = 1; i < atten.size(); ++i)
            if (atten[i] < peakAtten) { peakAtten = atten[i]; peakIdx = static_cast<int> (i); }

        // Plateau: walk forward from the peak until the attenuation stops
        // strictly increasing — that's the start of the sustain (or, if no
        // decay, the peak itself). For a flat macro this collapses to the
        // peak with sus = 0 (no decay).
        int plateauIdx   = peakIdx;
        int plateauAtten = peakAtten;
        for (std::size_t i = static_cast<std::size_t> (peakIdx) + 1; i < atten.size(); ++i)
        {
            if (atten[i] <= plateauAtten) break;
            plateauAtten = atten[i];
            plateauIdx   = static_cast<int> (i);
        }

        // Release tail: anything past the plateau that climbs back up to a
        // higher attenuation (closer to silence) than the plateau. A flat
        // tail keeps the mid-range default so notes don't cut off abruptly.
        int releaseSteps = 0;
        for (std::size_t i = static_cast<std::size_t> (plateauIdx) + 1; i < atten.size(); ++i)
            if (atten[i] > plateauAtten) ++releaseSteps;

        const int atkSteps = peakIdx;
        const int dr1Steps = std::max (0, plateauIdx - peakIdx);
        const int susDrop  = std::clamp (plateauAtten - peakAtten, 0, 15);
        const int rrSteps  = releaseSteps == 0 ? 5 : releaseSteps;

        // Direct step → ADSR mapping. DMP STD volume macros at Genesis tick
        // around 60 Hz, so a 30-step ramp ≈ 0.5 s — close enough to the
        // SN76489Engine atk/dr/rr "0..max ≈ 2 s" curve that step counts
        // double sensibly into the wider 0..31 ranges. The cap keeps absurd
        // macros from saturating to maximum.
        tone.atk = std::clamp (atkSteps,       0, 31);
        tone.dr1 = std::clamp (dr1Steps,       0, 31);
        tone.sus = susDrop;                                // already 0..15
        tone.dr2 = 0;                                      // not detected — ADR-0026
        tone.rr  = std::clamp (rrSteps,        0, 15);

        // Per-channel volume/pan/detune defaults (ADR-0026 *Macro → ADSR
        // approximation*, steps 7 & 8): vol = 1, pan = 0, detune = 0.
        tone.vol    = 1.0f;
        tone.pan    = 0.0f;
        tone.detune = 0;
    }
}

PsgPresetLoadResult loadDmpPsg (const std::filesystem::path& path)
{
    std::ifstream file (path, std::ios::binary);
    if (! file)
        return { std::nullopt, "cannot open file: " + path.string(), {} };

    const std::istreambuf_iterator<char> first (file);
    const std::istreambuf_iterator<char> last;
    const std::vector<std::uint8_t> bytes (first, last);

    if (bytes.size() < 3)
        return { std::nullopt,
                 "DMP file too short (" + std::to_string (bytes.size())
                     + " bytes); need at least 3 for header",
                 {} };

    if (bytes[0] != kDmpVersion)
        return { std::nullopt,
                 "DMP version " + std::to_string (static_cast<int> (bytes[0]))
                     + " not supported; only version 11 is accepted (ADR-0012)",
                 {} };

    if (bytes[1] != kDmpSysGenesis && bytes[1] != kDmpSysGenesisExt)
        return { std::nullopt,
                 "DMP system byte " + std::to_string (static_cast<int> (bytes[1]))
                     + " not supported; expected 0x02 or 0x42 (Genesis)",
                 {} };

    if (bytes[2] != kDmpModePsg)
        return { std::nullopt,
                 "DMP mode " + std::to_string (static_cast<int> (bytes[2]))
                     + " not supported by the PSG loader; only mode 0 (STD/PSG) is accepted",
                 {} };

    // Body: volume / arp / duty / wave macros in that order. The byte layout
    // mirrors Furnace's DivEngine::loadDMP STD branch (see ADR-0012 pattern
    // / Furnace `src/engine/fileOpsIns.cpp`). Tracker chip-specific tails
    // (C64, GB) are not present for Genesis PSG (DIV_INS_STD).
    ByteCursor cur { &bytes, 3, true };
    std::vector<std::int32_t> volMacro, arpMacro;

    if (! readDmpStdMacro (cur, volMacro))
        return { std::nullopt, "DMP PSG: truncated volume macro", {} };

    if (! readDmpStdMacro (cur, arpMacro))
        return { std::nullopt, "DMP PSG: truncated arpeggio macro", {} };

    // arpMacro.mode (1 byte) lives directly after the arp loop point in v11.
    {
        std::uint8_t arpMode = 0;
        if (! cur.readByte (arpMode))
            return { std::nullopt, "DMP PSG: missing arpeggio macro mode", {} };
    }

    if (! skipDmpStdMacro (cur))
        return { std::nullopt, "DMP PSG: truncated duty macro", {} };

    if (! skipDmpStdMacro (cur))
        return { std::nullopt, "DMP PSG: truncated wave macro", {} };

    // Build the preset. ADR-0026 step 7: vol = 1 for all channels. To make
    // the preset audibly different from "default lead", apply the imported
    // envelope to all three tone channels at unity volume so the user hears
    // the macro's character immediately. Noise channel stays silent — DMP
    // PSG instruments target tone channels.
    PsgPreset preset;
    preset.version = 1;
    preset.name    = path.stem().string();

    PsgPresetChannel toneTemplate {};
    approximateAdsr (volMacro, toneTemplate);

    for (int i = 0; i < PsgPreset::kNumTones; ++i)
        preset.tones[(std::size_t) i] = toneTemplate;

    // Noise channel: silent, defaults. DMP PSG does not carry a separate
    // SN76489 noise-mode field that maps cleanly to noise.type / noise.rate
    // (DefleMask exposes those through tracker effects, not the instrument
    // body), so we leave the noise channel at the apvts defaults.
    preset.noise          = PsgPresetChannel{};
    preset.noise.vol      = 0.0f;
    preset.noise.rr       = 4;
    preset.noiseType      = "white";
    preset.noiseRate      = "mid";

    PsgPresetLoadResult result;
    result.preset = std::move (preset);
    if (! arpMacro.empty())
        result.warning = "DMP PSG arpeggio / pitch macro ignored — only volume envelope imported.";

    return result;
}
