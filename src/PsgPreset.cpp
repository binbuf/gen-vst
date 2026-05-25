#include "PsgPreset.h"

#include <algorithm>
#include <fstream>
#include <sstream>

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
