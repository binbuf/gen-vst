#include "Kit.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <sstream>

#include <juce_core/juce_core.h>

namespace
{
    template <typename T>
    T clampVal (T v, T lo, T hi) noexcept { return std::clamp (v, lo, hi); }

    // --- juce::var readers (mirror PsgPreset.cpp) -----------------------------

    juce::var getChild (const juce::var& parent, const char* key) noexcept
    {
        if (! parent.isObject()) return {};
        const auto* dyn = parent.getDynamicObject();
        if (dyn == nullptr) return {};
        return dyn->getProperty (juce::Identifier (key));
    }

    bool hasField (const juce::var& obj, const char* key) noexcept
    {
        if (! obj.isObject()) return false;
        const auto* dyn = obj.getDynamicObject();
        return dyn != nullptr && dyn->hasProperty (juce::Identifier (key));
    }

    int readInt (const juce::var& obj, const char* key, int fallback) noexcept
    {
        const auto v = getChild (obj, key);
        if (v.isInt() || v.isDouble() || v.isInt64()) return static_cast<int> (v);
        return fallback;
    }

    float readFloat (const juce::var& obj, const char* key, float fallback) noexcept
    {
        const auto v = getChild (obj, key);
        if (v.isDouble() || v.isInt() || v.isInt64()) return static_cast<float> ((double) v);
        return fallback;
    }

    bool readBool (const juce::var& obj, const char* key, bool fallback) noexcept
    {
        const auto v = getChild (obj, key);
        if (v.isBool()) return static_cast<bool> (v);
        if (v.isInt() || v.isDouble() || v.isInt64()) return static_cast<int> (v) != 0;
        return fallback;
    }

    std::string readStr (const juce::var& obj, const char* key, const char* fallback)
    {
        const auto v = getChild (obj, key);
        if (v.isString()) return v.toString().toStdString();
        return fallback;
    }

    // --- Patch <-> juce::var --------------------------------------------------
    //
    // Every field of the in-memory Patch is serialized as-is (TL/SL stay
    // hardware-attenuation — kits never touch the apvts UI-level params, so no
    // inversion happens here). patchFromVar clamps each field to its hardware
    // range so a hand-edited or corrupt kit cannot emit junk register writes.

    juce::var opToVar (const Patch& p, int i)
    {
        const auto u = (std::size_t) i;
        auto* o = new juce::DynamicObject();
        o->setProperty ("mul",  p.mul[u]);
        o->setProperty ("dt",   p.dt[u]);
        o->setProperty ("tl",   p.tl[u]);
        o->setProperty ("ks",   p.ks[u]);
        o->setProperty ("ar",   p.ar[u]);
        o->setProperty ("dr",   p.dr[u]);
        o->setProperty ("sr",   p.sr[u]);
        o->setProperty ("rr",   p.rr[u]);
        o->setProperty ("sl",   p.sl[u]);
        o->setProperty ("ssg",  p.ssg[u]);
        o->setProperty ("amon", p.amon[u]);
        o->setProperty ("mulFloat",    (double) p.mul_float[u]);
        o->setProperty ("fixed",       p.fixed[u]);
        o->setProperty ("freqFixedHz", (double) p.freq_fixed_hz[u]);
        o->setProperty ("vel",         (double) p.vel[u]);
        return juce::var (o);
    }

    juce::var patchToVar (const Patch& p)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("alg",        p.alg);
        o->setProperty ("fb",         p.fb);
        o->setProperty ("lr",         p.lr);
        o->setProperty ("ams",        p.ams);
        o->setProperty ("pms",        p.pms);
        o->setProperty ("lfoEnable",  p.lfo_enable);
        o->setProperty ("lfoRate",    p.lfo_rate);
        o->setProperty ("freqCtrlMode", p.freq_ctrl_mode);
        o->setProperty ("retrigRate",   p.retrig_rate);
        o->setProperty ("channelTl",     (double) p.channel_tl);
        o->setProperty ("fmDacPrescaler",(double) p.fm_dac_prescaler);
        o->setProperty ("name",       juce::String (p.name));

        juce::Array<juce::var> ops;
        for (int i = 0; i < 4; ++i)
            ops.add (opToVar (p, i));
        o->setProperty ("op", ops);
        return juce::var (o);
    }

    void opFromVar (const juce::var& v, Patch& p, int i)
    {
        const auto u = (std::size_t) i;
        p.mul[u]  = (std::uint8_t) clampVal (readInt (v, "mul",  0), 0, 15);
        p.dt[u]   = (std::uint8_t) clampVal (readInt (v, "dt",   0), 0, 6);
        p.tl[u]   = (std::uint8_t) clampVal (readInt (v, "tl",   0), 0, 127);
        p.ks[u]   = (std::uint8_t) clampVal (readInt (v, "ks",   0), 0, 3);
        p.ar[u]   = (std::uint8_t) clampVal (readInt (v, "ar",   0), 0, 31);
        p.dr[u]   = (std::uint8_t) clampVal (readInt (v, "dr",   0), 0, 31);
        p.sr[u]   = (std::uint8_t) clampVal (readInt (v, "sr",   0), 0, 31);
        p.rr[u]   = (std::uint8_t) clampVal (readInt (v, "rr",   0), 0, 15);
        p.sl[u]   = (std::uint8_t) clampVal (readInt (v, "sl",   0), 0, 15);
        const int ssg = clampVal (readInt (v, "ssg", 0), 0, 15);
        p.ssg[u]  = (std::uint8_t) ((ssg >= 1 && ssg <= 7) ? 0 : ssg);   // 1-7 invalid
        p.amon[u] = (std::uint8_t) clampVal (readInt (v, "amon", 0), 0, 1);

        p.mul_float[u]     = clampVal (readFloat (v, "mulFloat", 1.0f), 0.0f, 15.0f);
        p.fixed[u]         = readBool  (v, "fixed", false);
        p.freq_fixed_hz[u] = clampVal (readFloat (v, "freqFixedHz", 440.0f), 0.0f, 20000.0f);
        p.vel[u]           = clampVal (readFloat (v, "vel", 0.0f), 0.0f, 1.0f);
    }

    Patch patchFromVar (const juce::var& v)
    {
        Patch p {};
        p.alg        = (std::uint8_t) clampVal (readInt (v, "alg", 0), 0, 7);
        p.fb         = (std::uint8_t) clampVal (readInt (v, "fb",  0), 0, 7);
        p.lr         = (std::uint8_t) clampVal (readInt (v, "lr",  3), 0, 3);
        p.ams        = (std::uint8_t) clampVal (readInt (v, "ams", 0), 0, 3);
        p.pms        = (std::uint8_t) clampVal (readInt (v, "pms", 0), 0, 7);
        p.lfo_enable = (std::uint8_t) clampVal (readInt (v, "lfoEnable", 0), 0, 1);
        p.lfo_rate   = (std::uint8_t) clampVal (readInt (v, "lfoRate",   0), 0, 7);

        p.freq_ctrl_mode = (std::uint8_t) clampVal (readInt (v, "freqCtrlMode", 0), 0, 2);
        p.retrig_rate    = (std::uint16_t) clampVal (readInt (v, "retrigRate", 500), 0, 1023);
        p.channel_tl       = clampVal (readFloat (v, "channelTl", 1.0f), 0.0f, 1.0f);
        p.fm_dac_prescaler = clampVal (readFloat (v, "fmDacPrescaler", 0.0f), 0.0f, 1.0f);
        p.name = readStr (v, "name", "");

        const auto ops = getChild (v, "op");
        if (auto* arr = ops.getArray())
            for (int i = 0; i < 4 && i < arr->size(); ++i)
                opFromVar ((*arr)[i], p, i);
        return p;
    }

    // --- source resolution ----------------------------------------------------

    std::string lowerExt (const std::filesystem::path& path)
    {
        std::string ext = path.extension().string();
        std::transform (ext.begin(), ext.end(), ext.begin(),
                        [] (unsigned char c) { return (char) std::tolower (c); });
        return ext;
    }

    // --- slot JSON ------------------------------------------------------------

    juce::var slotToVar (int pad, const KitSlot& s)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("pad",       pad);
        o->setProperty ("note",      s.midiNote);
        o->setProperty ("fixedNote", s.fixedNote);
        o->setProperty ("label",     juce::String (s.label));
        o->setProperty ("volume",    (double) s.volume);
        o->setProperty ("decayRr",   s.decayRr);
        if (! s.sourcePath.empty())
            o->setProperty ("source", juce::String (s.sourcePath));
        o->setProperty ("patch", patchToVar (s.patch));
        return juce::var (o);
    }
}

PatchLoadResult loadKitSourcePatch (const std::filesystem::path& path)
{
    // `.psg` is SQ-only and is rejected — kits are FM-only. Mirrors
    // loadPresetFromPath's FM dispatch.
    std::string ext = path.extension().string();
    std::transform (ext.begin(), ext.end(), ext.begin(),
                    [] (unsigned char c) { return (char) std::tolower (c); });
    if (ext == ".tfi") return loadTFI (path);
    if (ext == ".vgi") return loadVGI (path);
    if (ext == ".dmp") return loadDMP (path);
    if (ext == ".y12") return loadY12 (path);
    if (ext == ".opm") return loadOPM (path);
    return { std::nullopt, "unsupported kit source format: " + ext };
}

int Kit::slotForNote (int note) const noexcept
{
    for (int i = 0; i < kNumPads; ++i)
        if (slots[(std::size_t) i].enabled && slots[(std::size_t) i].midiNote == note)
            return i;
    return -1;
}

Patch resolvedPadPatch (const KitSlot& slot)
{
    Patch p = slot.patch;
    p.channel_tl = std::clamp (p.channel_tl * std::clamp (slot.volume, 0.0f, 1.0f),
                               0.0f, 1.0f);
    if (slot.decayRr >= 0)
    {
        const auto rr = (std::uint8_t) std::clamp (slot.decayRr, 0, 15);
        for (auto& v : p.rr)
            v = rr;
    }
    return p;
}

KitLoadResult kitFromJson (const std::string& json, const std::filesystem::path& baseDir)
{
    juce::var parsed;
    const auto result = juce::JSON::parse (juce::String (json), parsed);
    if (result.failed() || ! parsed.isObject())
        return { std::nullopt,
                 "invalid .gnkit JSON: "
                     + (result.failed() ? result.getErrorMessage().toStdString()
                                        : std::string ("not an object")),
                 {} };

    const int version = readInt (parsed, "version", 1);
    if (version != 1)
        return { std::nullopt,
                 ".gnkit version " + std::to_string (version)
                     + " not supported (only version 1 is accepted)",
                 {} };

    Kit kit;
    kit.version = 1;
    kit.name    = readStr (parsed, "name", "");

    std::string warning;

    const auto slots = getChild (parsed, "slots");
    if (auto* arr = slots.getArray())
    {
        for (int i = 0; i < arr->size(); ++i)
        {
            const juce::var& sv = (*arr)[i];
            const int pad = readInt (sv, "pad", -1);
            if (pad < 0 || pad >= Kit::kNumPads)
                continue;   // out-of-range pad index — skip

            KitSlot slot;
            slot.midiNote   = clampVal (readInt (sv, "note", -1), -1, 127);
            slot.fixedNote  = clampVal (readInt (sv, "fixedNote", slot.midiNote), 0, 127);
            slot.label      = readStr  (sv, "label", "");
            slot.volume     = clampVal (readFloat (sv, "volume", 1.0f), 0.0f, 1.0f);
            slot.decayRr    = clampVal (readInt (sv, "decayRr", -1), -1, 15);
            slot.sourcePath = readStr  (sv, "source", "");

            if (hasField (sv, "patch"))
            {
                slot.patch = patchFromVar (getChild (sv, "patch"));
            }
            else if (! slot.sourcePath.empty())
            {
                // Resolve the source relative to the kit file, then embed.
                std::filesystem::path src { slot.sourcePath };
                if (src.is_relative())
                    src = baseDir / src;
                const auto loaded = loadKitSourcePatch (src);
                if (! loaded.patch.has_value())
                {
                    warning += "slot pad " + std::to_string (pad) + " (" + slot.label
                                 + "): " + loaded.error + "; ";
                    continue;   // leave the pad disabled
                }
                slot.patch = *loaded.patch;
                if (slot.patch.name.empty())
                    slot.patch.name = src.stem().string();
            }
            else
            {
                continue;   // neither embedded patch nor source — nothing to play
            }

            if (slot.label.empty())
                slot.label = slot.patch.name.empty() ? std::string ("Pad " + std::to_string (pad))
                                                     : slot.patch.name;
            slot.enabled = slot.midiNote >= 0;
            kit.slots[(std::size_t) pad] = std::move (slot);
        }
    }

    KitLoadResult out;
    out.kit     = std::move (kit);
    out.warning = warning;
    return out;
}

std::string kitToJson (const Kit& kit)
{
    juce::Array<juce::var> slotArr;
    for (int i = 0; i < Kit::kNumPads; ++i)
    {
        const auto& s = kit.slots[(std::size_t) i];
        if (s.enabled)
            slotArr.add (slotToVar (i, s));
    }

    auto* root = new juce::DynamicObject();
    root->setProperty ("version",     kit.version);
    root->setProperty ("name",        juce::String (kit.name));
    root->setProperty ("banks",       Kit::kNumBanks);
    root->setProperty ("padsPerBank", Kit::kPadsPerBank);
    root->setProperty ("slots",       slotArr);

    return juce::JSON::toString (juce::var (root), /* allOnOneLine */ false).toStdString();
}

KitLoadResult loadKit (const std::filesystem::path& path)
{
    std::ifstream file (path);
    if (! file)
        return { std::nullopt, "cannot open file: " + path.string(), {} };

    std::stringstream buffer;
    buffer << file.rdbuf();

    auto out = kitFromJson (buffer.str(), path.parent_path());
    if (out.kit.has_value() && out.kit->name.empty())
        out.kit->name = path.stem().string();
    return out;
}

std::string saveKit (const Kit& kit, const std::filesystem::path& path)
{
    const std::string json = kitToJson (kit);

    std::ofstream file (path, std::ios::binary | std::ios::trunc);
    if (! file)
        return "cannot open file for write: " + path.string();
    file << json;
    if (! file)
        return "write failed: " + path.string();
    return {};
}
