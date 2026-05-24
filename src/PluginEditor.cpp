#include "PluginEditor.h"

#include <cmath>
#include <filesystem>
#include <utility>

#include "BankIO.h"
#include "Tuning.h"
#include "VgmExtract.h"

#if ! GENVST_DEV_SERVER
 #include <memory>
 #include <optional>
 #include <unordered_map>
 #include <vector>

 #include "GenVstWebData.h"
#endif

namespace
{
    // The per-operator FM parameter IDs, in the relay-array order. Matches the
    // kOpParams table in PluginProcessor.cpp; the per-part suffix is built into
    // the apvts parameter ID but stripped from the relay name (the FM channel
    // paging contract — 05-ui-ux.md).
    constexpr std::array<const char*, GenVstAudioProcessorEditor::kNumOpParams>
        kFmOpParamIds {
            "dt", "mul", "tl", "ks", "ar", "dr", "sr", "rr", "sl", "ssg", "amon"
        };

    // The per-part FM parameter IDs, same convention.
    constexpr std::array<const char*, GenVstAudioProcessorEditor::kNumPartParams>
        kFmPartParamIds {
            "alg", "fb", "ams", "pms", "lr", "lfo_enable", "lfo_rate"
        };

    juce::String opParamIdForPart (const char* base, int op, int part)
    {
        return juce::String (base) + "_op" + juce::String (op + 1)
                                   + "_part" + juce::String (part + 1);
    }

    juce::String partParamIdForPart (const char* base, int part)
    {
        return juce::String (base) + "_part" + juce::String (part + 1);
    }
}

#if ! GENVST_DEV_SERVER
namespace
{
    // The embedded Vite bundle (genvst-ui.zip), opened once and shared by every
    // editor instance — the binary data is static, read-only and lives for the
    // lifetime of the process.
    juce::ZipFile& webBundle()
    {
        // BinaryData mangles "genvst-ui.zip" -> "genvstui_zip" (the hyphen is
        // dropped); see the generated GenVstWebData.h.
        static juce::MemoryInputStream stream { GenVstWebData::genvstui_zip,
                                                (size_t) GenVstWebData::genvstui_zipSize,
                                                false };
        static juce::ZipFile zip { stream };
        return zip;
    }

    // The WebKit backends (macOS, Linux) reject @font-face files served with a
    // wrong or missing MIME type where Chromium is lenient — every served
    // extension, fonts included, needs a correct type here (ADR-0015).
    juce::String mimeTypeForExtension (const juce::String& extension)
    {
        static const std::unordered_map<juce::String, juce::String> mimeTypes
        {
            { "html",  "text/html" },
            { "htm",   "text/html" },
            { "js",    "text/javascript" },
            { "mjs",   "text/javascript" },
            { "css",   "text/css" },
            { "json",  "application/json" },
            { "map",   "application/json" },
            { "svg",   "image/svg+xml" },
            { "png",   "image/png" },
            { "jpg",   "image/jpeg" },
            { "jpeg",  "image/jpeg" },
            { "gif",   "image/gif" },
            { "ico",   "image/vnd.microsoft.icon" },
            { "txt",   "text/plain" },
            { "woff",  "font/woff" },
            { "woff2", "font/woff2" },
            { "ttf",   "font/ttf" },
            { "otf",   "font/otf" },
        };

        const auto entry = mimeTypes.find (extension.toLowerCase());
        return entry != mimeTypes.end() ? entry->second
                                        : juce::String ("application/octet-stream");
    }

    // Resource provider: serves a file out of the embedded bundle. A request
    // for "/" maps to index.html (05-ui-ux.md "Resource delivery").
    std::optional<juce::WebBrowserComponent::Resource> getWebResource (const juce::String& url)
    {
        const auto path = (url == "/") ? juce::String ("index.html")
                                       : url.fromFirstOccurrenceOf ("/", false, false);

        auto& zip = webBundle();

        // `cmake -E tar ... .` may store entries with or without a leading "./".
        auto* entry = zip.getEntry (path);

        if (entry == nullptr)
            entry = zip.getEntry ("./" + path);

        if (entry == nullptr)
            return std::nullopt;

        const std::unique_ptr<juce::InputStream> stream { zip.createStreamForEntry (*entry) };

        if (stream == nullptr)
            return std::nullopt;

        std::vector<std::byte> data ((size_t) stream->getTotalLength());
        stream->setPosition (0);
        [[maybe_unused]] const auto bytesRead = stream->read (data.data(), (int) data.size());
        jassert (bytesRead == (int) data.size());

        return juce::WebBrowserComponent::Resource {
            std::move (data),
            mimeTypeForExtension (path.fromLastOccurrenceOf (".", false, false))
        };
    }
}
#endif

namespace
{
    // Build a tiny `{ ok: true|false, error?: "..." }` result var for the
    // load/save/import/export native functions. JS-side: `const r = await
    // loadInstrument(path); if (!r.ok) showToast(r.error);` is the standard
    // pattern.
    juce::var makeStatusVar (const std::string& errorMessage)
    {
        auto* obj = new juce::DynamicObject();
        obj->setProperty ("ok", errorMessage.empty());
        if (! errorMessage.empty())
            obj->setProperty ("error", juce::String (errorMessage));
        return juce::var (obj);
    }

    // Task 31 — build a `{ cells: [...] }` snapshot of the DAC kit for the JS
    // D-section view. Each cell holds either { empty: true } or { name,
    // lengthSec, rate, bitDepth, midiNote }. Peaks are omitted from the
    // per-cell snapshot (no per-cell waveform display yet) but the API is
    // shaped to allow them in later work.
    juce::var buildDacKitVar (const DACKit& kit)
    {
        juce::Array<juce::var> cells;
        for (int i = 0; i < DACKit::kNumCells; ++i)
        {
            auto* obj = new juce::DynamicObject();
            obj->setProperty ("index",    i);
            obj->setProperty ("midiNote", DACKit::noteForCellIndex (i));
            if (! kit.hasCell (i))
            {
                obj->setProperty ("empty", true);
            }
            else
            {
                const auto* c = kit.cellPtr (i);
                if (c != nullptr)
                {
                    obj->setProperty ("name",      c->name);
                    obj->setProperty ("lengthSec", kit.getSampleLengthSeconds (i));
                    obj->setProperty ("rate",      c->mtRate);
                    obj->setProperty ("bitDepth",  8);
                }
            }
            cells.add (juce::var (obj));
        }

        auto* out = new juce::DynamicObject();
        out->setProperty ("cells", juce::var (cells));
        return juce::var (out);
    }

    // --- Task 33 — Per-slot copy/paste helpers --------------------------------
    // captureSlotState builds a JSON-serialisable juce::var snapshot of every
    // parameter that defines a rack slot's identity (Patch + routing + polyphony
    // for FM; PSG envelope + routing for SQ; DACKit cells + routing for D). The
    // clipboard payload is opaque to JS — round-tripped through copySlot ->
    // pasteSlot — so its layout only has to be stable within a single editor
    // session. applySlotState replays the snapshot through setValueNotifyingHost
    // (and DACKit::loadCellRawPcm for D rows), the same path bank-import uses
    // so the audio thread picks up every change via the standard apvts ->
    // FmParamCache pipeline.

    // Snapshot every apvts param in `ids` under the suffix into an object var.
    juce::var snapshotParams (juce::AudioProcessorValueTreeState& apvts,
                              const juce::String&                  suffix,
                              const std::vector<const char*>&      ids)
    {
        auto* obj = new juce::DynamicObject();
        for (const char* id : ids)
        {
            const juce::String fullId = juce::String (id) + suffix;
            if (auto* p = apvts.getRawParameterValue (fullId))
                obj->setProperty (id, (double) p->load());
        }
        return juce::var (obj);
    }

    // Replay every property of `paramsVar` onto the apvts param `<key><suffix>`.
    void applyParams (juce::AudioProcessorValueTreeState& apvts,
                      const juce::String&                  suffix,
                      const juce::var&                     paramsVar)
    {
        auto* obj = paramsVar.getDynamicObject();
        if (obj == nullptr) return;
        for (const auto& prop : obj->getProperties())
        {
            const juce::String fullId = prop.name.toString() + suffix;
            if (auto* p = apvts.getParameter (fullId))
            {
                const float v = (float) (double) prop.value;
                p->setValueNotifyingHost (p->convertTo0to1 (v));
            }
        }
    }

    // Per-slot apvts param IDs (suffix-relative). Routing applies to every
    // slot type; the rest are per-type. Stays in sync with createParameterLayout
    // in PluginProcessor.cpp.
    const std::vector<const char*>& routingParamIds()
    {
        static const std::vector<const char*> ids {
            "midi_ch", "transpose_st", "transpose_oct",
            "note_lo", "note_hi", "detune_cents", "balance"
        };
        return ids;
    }

    const std::vector<const char*>& fmPolyphonyParamIds()
    {
        static const std::vector<const char*> ids {
            "poly_mode", "mono_glide", "unison_spread", "glide_time"
        };
        return ids;
    }

    const std::vector<const char*>& psgChannelParamIds()
    {
        static const std::vector<const char*> ids {
            "psg_vol", "psg_pan", "psg_bend",
            "psg_atk", "psg_dr1", "psg_sus", "psg_dr2", "psg_rr",
            "psg_detune", "psg_freq", "psg_ksr", "psg_ssg", "psg_vel"
        };
        return ids;
    }

    // Serialise a Patch struct into a juce::var (object with per-field
    // arrays for the per-op data, scalars for the per-part data). Mirrors
    // the patch model in PatchSystem.h.
    juce::var patchToVar (const Patch& patch)
    {
        auto* obj = new juce::DynamicObject();
        obj->setProperty ("name",       juce::String (patch.name));
        obj->setProperty ("alg",        (int) patch.alg);
        obj->setProperty ("fb",         (int) patch.fb);
        obj->setProperty ("lr",         (int) patch.lr);
        obj->setProperty ("ams",        (int) patch.ams);
        obj->setProperty ("pms",        (int) patch.pms);
        obj->setProperty ("lfo_enable", (int) patch.lfo_enable);
        obj->setProperty ("lfo_rate",   (int) patch.lfo_rate);

        auto packOps = [] (const std::uint8_t (&src)[4])
        {
            juce::Array<juce::var> arr;
            for (int i = 0; i < 4; ++i) arr.add ((int) src[i]);
            return juce::var (arr);
        };
        obj->setProperty ("mul",  packOps (patch.mul));
        obj->setProperty ("dt",   packOps (patch.dt));
        obj->setProperty ("tl",   packOps (patch.tl));
        obj->setProperty ("ks",   packOps (patch.ks));
        obj->setProperty ("ar",   packOps (patch.ar));
        obj->setProperty ("dr",   packOps (patch.dr));
        obj->setProperty ("sr",   packOps (patch.sr));
        obj->setProperty ("rr",   packOps (patch.rr));
        obj->setProperty ("sl",   packOps (patch.sl));
        obj->setProperty ("ssg",  packOps (patch.ssg));
        obj->setProperty ("amon", packOps (patch.amon));
        return juce::var (obj);
    }

    Patch varToPatch (const juce::var& v)
    {
        Patch p;
        auto* obj = v.getDynamicObject();
        if (obj == nullptr) return p;

        auto readByte = [obj] (const char* key, std::uint8_t dflt) -> std::uint8_t
        {
            if (! obj->hasProperty (key)) return dflt;
            return (std::uint8_t) juce::jlimit (0, 255, (int) obj->getProperty (key));
        };
        if (obj->hasProperty ("name"))
            p.name = obj->getProperty ("name").toString().toStdString();
        p.alg        = readByte ("alg",        0);
        p.fb         = readByte ("fb",         0);
        p.lr         = readByte ("lr",         3);
        p.ams        = readByte ("ams",        0);
        p.pms        = readByte ("pms",        0);
        p.lfo_enable = readByte ("lfo_enable", 0);
        p.lfo_rate   = readByte ("lfo_rate",   0);

        auto unpackOps = [obj] (const char* key, std::uint8_t (&dst)[4])
        {
            if (! obj->hasProperty (key)) return;
            auto v = obj->getProperty (key);
            if (auto* arr = v.getArray())
                for (int i = 0; i < juce::jmin (4, arr->size()); ++i)
                    dst[i] = (std::uint8_t) juce::jlimit (0, 255, (int) (*arr)[i]);
        };
        unpackOps ("mul",  p.mul);
        unpackOps ("dt",   p.dt);
        unpackOps ("tl",   p.tl);
        unpackOps ("ks",   p.ks);
        unpackOps ("ar",   p.ar);
        unpackOps ("sr",   p.sr);
        unpackOps ("dr",   p.dr);
        unpackOps ("rr",   p.rr);
        unpackOps ("sl",   p.sl);
        unpackOps ("ssg",  p.ssg);
        unpackOps ("amon", p.amon);
        return p;
    }

    // PSG sub-id strings keyed by SlotId.index (matches kPsgRackIds in
    // PluginEditor's getRackState).
    const std::array<const char*, SN76489Engine::kNumChannels>& psgSubIds()
    {
        static const std::array<const char*, SN76489Engine::kNumChannels>
            ids { "ch1", "ch2", "ch3", "noise" };
        return ids;
    }

    // Build the apvts suffix for a slot ("_part1", "_psg_ch1", "_dac", ...).
    juce::String suffixForSlot (PartManager::SlotId slot)
    {
        switch (slot.type)
        {
            case PartManager::InstrumentType::FM:
                return "_part" + juce::String (slot.index + 1);
            case PartManager::InstrumentType::SQ:
                return juce::String ("_psg_") + psgSubIds()[(std::size_t) slot.index];
            case PartManager::InstrumentType::D:
                return "_dac";
        }
        return {};
    }
}

std::vector<std::unique_ptr<juce::WebSliderRelay>>
GenVstAudioProcessorEditor::makeOpRelays()
{
    std::vector<std::unique_ptr<juce::WebSliderRelay>> result;
    result.reserve (kNumOps * kNumOpParams);
    for (int op = 0; op < kNumOps; ++op)
        for (int p = 0; p < kNumOpParams; ++p)
            result.push_back (std::make_unique<juce::WebSliderRelay> (
                juce::String (kFmOpParamIds[(std::size_t) p])
                    + "_op" + juce::String (op + 1)));
    return result;
}

std::vector<std::unique_ptr<juce::WebSliderRelay>>
GenVstAudioProcessorEditor::makePartRelays()
{
    std::vector<std::unique_ptr<juce::WebSliderRelay>> result;
    result.reserve (kNumPartParams);
    for (int p = 0; p < kNumPartParams; ++p)
        result.push_back (std::make_unique<juce::WebSliderRelay> (
            juce::String (kFmPartParamIds[(std::size_t) p])));
    return result;
}

namespace
{
    constexpr std::array<const char*, SN76489Engine::kNumChannels> kPsgChannelIds {
        "ch1", "ch2", "ch3", "noise"
    };
}

std::array<std::unique_ptr<juce::WebSliderRelay>, SN76489Engine::kNumChannels>
GenVstAudioProcessorEditor::makePsgVolRelays()
{
    std::array<std::unique_ptr<juce::WebSliderRelay>, SN76489Engine::kNumChannels> result;
    for (int i = 0; i < SN76489Engine::kNumChannels; ++i)
        result[(std::size_t) i] = std::make_unique<juce::WebSliderRelay> (
            juce::String ("psg_vol_") + kPsgChannelIds[(std::size_t) i]);
    return result;
}

std::array<std::unique_ptr<juce::WebSliderRelay>, SN76489Engine::kNumChannels>
GenVstAudioProcessorEditor::makePsgPanRelays()
{
    std::array<std::unique_ptr<juce::WebSliderRelay>, SN76489Engine::kNumChannels> result;
    for (int i = 0; i < SN76489Engine::kNumChannels; ++i)
        result[(std::size_t) i] = std::make_unique<juce::WebSliderRelay> (
            juce::String ("psg_pan_") + kPsgChannelIds[(std::size_t) i]);
    return result;
}

std::array<std::unique_ptr<juce::WebToggleButtonRelay>, SN76489Engine::kNumChannels>
GenVstAudioProcessorEditor::makePsgBendRelays()
{
    std::array<std::unique_ptr<juce::WebToggleButtonRelay>, SN76489Engine::kNumChannels> result;
    for (int i = 0; i < SN76489Engine::kNumChannels; ++i)
        result[(std::size_t) i] = std::make_unique<juce::WebToggleButtonRelay> (
            juce::String ("psg_bend_") + kPsgChannelIds[(std::size_t) i]);
    return result;
}

namespace
{
    // Task 23 — per-channel envelope slider relay names, by paramIdx. The
    // order is shared between the relay factory, the attachment loop and the
    // JS-side OperatorPanel binding map in sq-view.js.
    constexpr std::array<const char*, GenVstAudioProcessorEditor::kNumPsgEnvSliderParams>
        kPsgEnvSliderBases {
            "psg_atk", "psg_dr1", "psg_sus", "psg_dr2", "psg_rr",
            "psg_detune", "psg_freq", "psg_ksr", "psg_ssg"
        };
}

std::array<std::array<std::unique_ptr<juce::WebSliderRelay>,
                     SN76489Engine::kNumChannels>,
          GenVstAudioProcessorEditor::kNumPsgEnvSliderParams>
GenVstAudioProcessorEditor::makePsgEnvSliderRelays()
{
    std::array<std::array<std::unique_ptr<juce::WebSliderRelay>,
                          SN76489Engine::kNumChannels>,
               kNumPsgEnvSliderParams> result;
    for (int p = 0; p < kNumPsgEnvSliderParams; ++p)
        for (int i = 0; i < SN76489Engine::kNumChannels; ++i)
            result[(std::size_t) p][(std::size_t) i] =
                std::make_unique<juce::WebSliderRelay> (
                    juce::String (kPsgEnvSliderBases[(std::size_t) p])
                        + "_" + kPsgChannelIds[(std::size_t) i]);
    return result;
}

std::array<std::unique_ptr<juce::WebToggleButtonRelay>, SN76489Engine::kNumChannels>
GenVstAudioProcessorEditor::makePsgEnvVelRelays()
{
    std::array<std::unique_ptr<juce::WebToggleButtonRelay>,
               SN76489Engine::kNumChannels> result;
    for (int i = 0; i < SN76489Engine::kNumChannels; ++i)
        result[(std::size_t) i] = std::make_unique<juce::WebToggleButtonRelay> (
            juce::String ("psg_vel_") + kPsgChannelIds[(std::size_t) i]);
    return result;
}

namespace
{
    // Task 22 — Per-rack-slot routing relay schema. The slot-suffix list +
    // param-base list together generate the 7 × 11 = 77 apvts param IDs the
    // rack widget binds to.
    constexpr std::array<const char*, GenVstAudioProcessorEditor::kNumRackParamsPerSlot>
        kRackRoutingParamBases {
            "midi_ch", "transpose_st", "transpose_oct",
            "note_lo", "note_hi", "detune_cents", "balance"
        };

    // Build the 11 slot suffixes in a stable order: FM parts 1..6, PSG ch1..3,
    // PSG noise, DAC. This is the index layout used by every per-rack-slot
    // array in PluginEditor (relays + attachments).
    std::array<juce::String, GenVstAudioProcessorEditor::kNumRackSlotSuffixes>
        rackRoutingSuffixes()
    {
        std::array<juce::String, GenVstAudioProcessorEditor::kNumRackSlotSuffixes> out;
        std::size_t idx = 0;
        for (int p = 0; p < PartManager::kNumParts; ++p)
            out[idx++] = "_part" + juce::String (p + 1);
        out[idx++] = "_psg_ch1";
        out[idx++] = "_psg_ch2";
        out[idx++] = "_psg_ch3";
        out[idx++] = "_psg_noise";
        out[idx++] = "_dac";
        jassert (idx == GenVstAudioProcessorEditor::kNumRackSlotSuffixes);
        return out;
    }
}

std::array<std::unique_ptr<juce::WebSliderRelay>,
           (std::size_t) (GenVstAudioProcessorEditor::kNumRackParamsPerSlot
                          * GenVstAudioProcessorEditor::kNumRackSlotSuffixes)>
GenVstAudioProcessorEditor::makeRackRoutingRelays()
{
    std::array<std::unique_ptr<juce::WebSliderRelay>,
               (std::size_t) (kNumRackParamsPerSlot * kNumRackSlotSuffixes)> result;
    const auto suffixes = rackRoutingSuffixes();
    std::size_t idx = 0;
    for (std::size_t s = 0; s < (std::size_t) kNumRackSlotSuffixes; ++s)
        for (std::size_t p = 0; p < (std::size_t) kNumRackParamsPerSlot; ++p)
            result[idx++] = std::make_unique<juce::WebSliderRelay> (
                juce::String (kRackRoutingParamBases[p]) + suffixes[s]);
    return result;
}

namespace
{
    juce::var makeDestinationVar (MidiRouter::Destination dest)
    {
        const char* kind = "off";
        int         idx  = 0;
        switch (dest.kind)
        {
            case MidiRouter::Destination::Kind::FmPart:   kind = "fm";        idx = dest.index; break;
            case MidiRouter::Destination::Kind::PsgTone:  kind = "psg-tone";  idx = dest.index; break;
            case MidiRouter::Destination::Kind::PsgNoise: kind = "psg-noise"; idx = 0; break;
            case MidiRouter::Destination::Kind::Dac:      kind = "dac";       idx = 0; break;
            default: break;
        }
        auto* obj = new juce::DynamicObject();
        obj->setProperty ("kind",  juce::String (kind));
        obj->setProperty ("index", idx);
        return juce::var (obj);
    }

    MidiRouter::Destination destinationFromKindIndex (const juce::String& kind, int index)
    {
        using Kind = MidiRouter::Destination::Kind;
        if (kind == "fm")        return { Kind::FmPart,   index };
        if (kind == "psg-tone")  return { Kind::PsgTone,  index };
        if (kind == "psg-noise") return { Kind::PsgNoise, 0 };
        if (kind == "dac")       return { Kind::Dac,      0 };
        return {};
    }
}

juce::WebBrowserComponent::Options GenVstAudioProcessorEditor::makeOptions()
{
    auto options = juce::WebBrowserComponent::Options{}
        .withBackend (juce::WebBrowserComponent::Options::Backend::webview2)
        .withWinWebView2Options (juce::WebBrowserComponent::Options::WinWebView2{}
            // Plugin processes are denied the default WebView2 user-data
            // location; point it at the temp dir or it can fail to initialise.
            .withUserDataFolder (juce::File::getSpecialLocation (
                juce::File::SpecialLocationType::tempDirectory))
            .withBackgroundColour (juce::Colours::black))
        .withNativeIntegrationEnabled()
        .withOptionsFrom (masterGainRelay)
        // Task 13 — global PSG / DAC / Settings relays.
        .withOptionsFrom (psgMixRelay)
        .withOptionsFrom (psgLayerRelay)
        .withOptionsFrom (psgNoiseTypeRelay)
        .withOptionsFrom (psgNoiseRateRelay)
        .withOptionsFrom (psgNoiseAutoRelay)
        .withOptionsFrom (dacEnableRelay)
        .withOptionsFrom (dacRateRelay)
        .withOptionsFrom (dacModeRelay)
        .withOptionsFrom (dacLevelRelay)
        .withOptionsFrom (bendRangeRelay)
        .withOptionsFrom (velToTlRelay)
        .withOptionsFrom (trueStereoRelay)
        .withOptionsFrom (aftertouchTargetRelay)
        .withOptionsFrom (voiceCountRelay)
        .withOptionsFrom (uiScaleRelay)
        .withOptionsFrom (tooltipsEnabledRelay)
        // View 10 polyphony relays — names are stripped (`poly_mode`,
        // `mono_glide`, `unison_spread`); the attachments rebind on selectChannel.
        .withOptionsFrom (polyModeRelay)
        .withOptionsFrom (monoGlideRelay)
        .withOptionsFrom (unisonSpreadRelay)
       #if GENVST_DEV_SERVER
        // Widget gallery relays (Task 10) — dev-server builds only.
        .withOptionsFrom (galleryKnobRelay)
        .withOptionsFrom (gallerySliderRelay)
        .withOptionsFrom (galleryReadoutRelay)
        .withOptionsFrom (galleryStepRelay)
        .withOptionsFrom (galleryToggleRelay)
        .withOptionsFrom (gallerySectionRelay)
        .withOptionsFrom (galleryTabsRelay)
        .withOptionsFrom (galleryListRelay)
       #endif
        .withEventListener ("uiReady", [] (juce::var)
        {
            juce::Logger::writeToLog ("Gen VST: uiReady received from WebView");
        });

    // Register every FM relay. They're already pinned on the heap via the
    // unique_ptr vector, so the references handed in here stay valid for the
    // editor's lifetime.
    for (auto& r : opRelays)
        options = options.withOptionsFrom (*r);
    for (auto& r : partRelays)
        options = options.withOptionsFrom (*r);

    // Per-PSG-channel relays — same heap-pinning lifetime as the FM relays.
    for (auto& r : psgVolRelays)  options = options.withOptionsFrom (*r);
    for (auto& r : psgPanRelays)  options = options.withOptionsFrom (*r);
    for (auto& r : psgBendRelays) options = options.withOptionsFrom (*r);

    // Task 23 — per-channel envelope relays. 9 slider params × 4 channels
    // for ATK/DR1/SUS/DR2/RR/DETUNE/FREQ/KSR/SSG, plus 4 toggle relays for
    // VEL (one per channel).
    for (auto& row : psgEnvSliderRelays)
        for (auto& r : row)
            options = options.withOptionsFrom (*r);
    for (auto& r : psgEnvVelRelays) options = options.withOptionsFrom (*r);

    // Task 22 — Per-rack-slot routing relays (77 sliders covering every
    // (midi_ch, transpose_st, transpose_oct, note_lo, note_hi, detune_cents,
    // balance) × (FM part / PSG channel / DAC) pair).
    for (auto& r : rackRoutingRelays) options = options.withOptionsFrom (*r);

    using Completion = juce::WebBrowserComponent::NativeFunctionCompletion;

    // --- Folder-tree queries -------------------------------------------------
    options = options.withNativeFunction ("getRoots",
        [this] (const juce::Array<juce::var>&, Completion completion)
        {
            completion (processor.getPatchBrowser().rootsAsJson());
        });

    options = options.withNativeFunction ("getPatchList",
        [this] (const juce::Array<juce::var>& args, Completion completion)
        {
            // No-arg form -> top-level roots; with a folder path, the children
            // of that folder. Triggers a lazy scan if the folder isn't yet.
            if (args.isEmpty() || ! args[0].isString())
                completion (processor.getPatchBrowser().rootsAsJson());
            else
                completion (processor.getPatchBrowser().folderAsJson (args[0].toString()));
        });

    options = options.withNativeFunction ("searchPatches",
        [this] (const juce::Array<juce::var>& args, Completion completion)
        {
            const auto q = args.isEmpty() || ! args[0].isString()
                               ? juce::String() : args[0].toString();
            completion (processor.getPatchBrowser().searchAsJson (q));
        });

    // --- Custom roots --------------------------------------------------------
    options = options.withNativeFunction ("addCustomRoot",
        [this] (const juce::Array<juce::var>& args, Completion completion)
        {
            if (args.isEmpty() || ! args[0].isString())
            { completion (makeStatusVar ("path required")); return; }
            const auto id = processor.getPatchBrowser().addCustomRoot (args[0].toString());
            if (id.isEmpty()) { completion (makeStatusVar ("could not register root")); return; }
            auto* obj = new juce::DynamicObject();
            obj->setProperty ("ok", true);
            obj->setProperty ("id", id);
            completion (juce::var (obj));
        });

    options = options.withNativeFunction ("removeCustomRoot",
        [this] (const juce::Array<juce::var>& args, Completion completion)
        {
            if (args.isEmpty() || ! args[0].isString())
            { completion (makeStatusVar ("id required")); return; }
            const auto ok = processor.getPatchBrowser().removeCustomRoot (args[0].toString());
            completion (makeStatusVar (ok ? std::string{} : std::string{"unknown root id"}));
        });

    // --- Patch loading (UI -> audio thread via the lock-free queue) ---------
    // Failures route through emitNotify so the toast (08-ui-views.md view 8)
    // surfaces patch-load errors automatically — JS does not need to inspect
    // the {ok,error} result.
    options = options.withNativeFunction ("loadInstrument",
        [this] (const juce::Array<juce::var>& args, Completion completion)
        {
            if (args.isEmpty() || ! args[0].isString())
            { completion (makeStatusVar ("path required")); return; }
            auto err = processor.getPatchBrowser().loadIntoPart (selectedPart, args[0].toString());
            if (! err.empty())
                emitNotify ("error", juce::String (err));
            completion (makeStatusVar (err));
        });

    // loadPreset is functionally identical to loadInstrument — the two-name
    // split exists only because the Genny layout has two separate LCD lists.
    // Folder-tree mode (ADR-0006) treats them as the same operation.
    options = options.withNativeFunction ("loadPreset",
        [this] (const juce::Array<juce::var>& args, Completion completion)
        {
            if (args.isEmpty() || ! args[0].isString())
            { completion (makeStatusVar ("path required")); return; }
            auto err = processor.getPatchBrowser().loadIntoPart (selectedPart, args[0].toString());
            if (! err.empty())
                emitNotify ("error", juce::String (err));
            completion (makeStatusVar (err));
        });

    // --- Save / Import / Export ---------------------------------------------
    options = options.withNativeFunction ("savePatch",
        [this] (const juce::Array<juce::var>& args, Completion completion)
        {
            const juce::String name = (! args.isEmpty() && args[0].isString())
                                          ? args[0].toString() : juce::String ("Patch");
            // Snapshot the live apvts so saves capture whatever the user is
            // hearing — CC edits, automation, the result of a load that came
            // through the queue, etc. — not a stale PartManager copy.
            Patch current;
            processor.readLivePatch (selectedPart, current);
            const auto  r       = processor.getPatchBrowser().savePatchAsTfi (current, name);
            if (! r.path.isEmpty())
            {
                emitPatchRootsChanged();
                auto* obj = new juce::DynamicObject();
                obj->setProperty ("ok",   true);
                obj->setProperty ("path", r.path);
                completion (juce::var (obj));
                return;
            }
            completion (makeStatusVar (r.error.empty() ? std::string{"save failed"} : r.error));
        });

    options = options.withNativeFunction ("importPatch",
        [this] (const juce::Array<juce::var>& args, Completion completion)
        {
            if (args.isEmpty() || ! args[0].isString())
            { completion (makeStatusVar ("path required")); return; }
            const auto err = processor.getPatchBrowser().importPatchFile (args[0].toString());
            if (! err.empty())
                emitNotify ("error", juce::String (err));
            else
                emitPatchRootsChanged();
            completion (makeStatusVar (err));
        });

    options = options.withNativeFunction ("exportPatch",
        [this] (const juce::Array<juce::var>& args, Completion completion)
        {
            if (args.isEmpty() || ! args[0].isString())
            { completion (makeStatusVar ("path required")); return; }
            Patch current;
            processor.readLivePatch (selectedPart, current);
            const auto err = processor.getPatchBrowser().exportPatchToPath (
                                 current, args[0].toString());
            completion (makeStatusVar (err));
        });

    // --- Patch browser — delete + native file choosers (Task 14) ------------
    // Delete uses the message-thread fs::remove path in PatchBrowser, which
    // rejects targets outside a writable root (Factory is read-only — the
    // Delete button in the modal is also disabled on the JS side, but the
    // backend check is the authoritative guard).
    options = options.withNativeFunction ("deletePatch",
        [this] (const juce::Array<juce::var>& args, Completion completion)
        {
            if (args.isEmpty() || ! args[0].isString())
            { completion (makeStatusVar ("path required")); return; }
            const auto err = processor.getPatchBrowser().deletePatchFile (args[0].toString());
            if (! err.empty())
                emitNotify ("error", juce::String (err));
            else
                emitPatchRootsChanged();
            completion (makeStatusVar (err));
        });

    // Import file dialog — open file, filter to the supported patch extensions,
    // copy into the user-imported root via importPatchFile. The filter literal
    // is built from kSupportedPatchExtensions so the IMPORT tab automatically
    // picks up any new format added to that constant.
    options = options.withNativeFunction ("importFileDialog",
        [this] (const juce::Array<juce::var>&, Completion completion)
        {
            importChooser = std::make_unique<juce::FileChooser> (
                "Import patch file", juce::File{},
                juce::String (buildPatchExtensionFilter()));

            const auto flags = juce::FileBrowserComponent::openMode
                             | juce::FileBrowserComponent::canSelectFiles;

            importChooser->launchAsync (flags,
                [this, completion = std::move (completion)] (const juce::FileChooser& fc) mutable
                {
                    const juce::File file = fc.getResult();
                    if (file == juce::File{})
                    { completion (makeStatusVar ({})); return; }   // cancelled

                    const auto err = processor.getPatchBrowser()
                                         .importPatchFile (file.getFullPathName());
                    if (! err.empty())
                        emitNotify ("error", juce::String (err));
                    else
                        emitPatchRootsChanged();
                    completion (makeStatusVar (err));
                });
        });

    // Import Bank dialog (Task 21 / ADR-0019, extended by Task 24). Two
    // accepted file flavours, branched on extension:
    //
    //   .vgm / .vgz                — Genny-style one-click FM-patch bank
    //                                 extraction. YM2612 register stream is
    //                                 parsed on a background thread; every
    //                                 unique key-on snapshot is written to
    //                                 the user-imported root.
    //   .gnbank / .json            — Restore a rack snapshot exported by
    //                                 Export Bank. Clears every rack slot,
    //                                 then replays each row (activate slot,
    //                                 reload the FM patch by path, write
    //                                 every per-instrument routing param).
    //
    // Either path ends with a toast and a patchRootsChanged refresh.
    options = options.withNativeFunction ("importBankDialog",
        [this] (const juce::Array<juce::var>&, Completion completion)
        {
            vgmImportChooser = std::make_unique<juce::FileChooser> (
                "Import Bank", juce::File{}, "*.vgm;*.vgz;*.gnbank;*.json");

            const auto flags = juce::FileBrowserComponent::openMode
                             | juce::FileBrowserComponent::canSelectFiles;

            vgmImportChooser->launchAsync (flags,
                [this, completion = std::move (completion)] (const juce::FileChooser& fc) mutable
                {
                    const juce::File file = fc.getResult();
                    if (file == juce::File{})
                    { completion (makeStatusVar ({})); return; }   // cancelled

                    const juce::String fileName = file.getFileName();
                    const juce::String ext      = file.getFileExtension().toLowerCase();

                    if (ext == ".vgm" || ext == ".vgz")
                    {
                        // Existing Task 21 path: extract every FM patch into
                        // the user-imported root.
                        runVgmExtractAsync (file.getFullPathName(),
                            [this, fileName, completion = std::move (completion)]
                            (int saved, const juce::String& err) mutable
                            {
                                if (saved > 0)
                                    emitNotify ("info",
                                        "Imported " + juce::String (saved)
                                            + (saved == 1 ? " patch from " : " patches from ")
                                            + fileName);
                                if (err.isNotEmpty())
                                    emitNotify ("error", err);
                                else if (saved == 0)
                                    emitNotify ("error", "No patches imported from " + fileName);

                                auto* obj = new juce::DynamicObject();
                                obj->setProperty ("ok",         (saved > 0) && err.isEmpty());
                                obj->setProperty ("savedCount", saved);
                                if (err.isNotEmpty())
                                    obj->setProperty ("error", err);
                                completion (juce::var (obj));
                            });
                        return;
                    }

                    // Task 24 — JSON bank restore. Parse the file, clear every
                    // rack slot, then replay each row. Errors collect into a
                    // single toast so the user sees the full picture rather
                    // than one toast per failed row.
                    genvst::bank::Bank bnk;
                    const auto err = genvst::bank::readFromFile (file, bnk);
                    if (err.isNotEmpty())
                    {
                        emitNotify ("error", err);
                        completion (makeStatusVar (err.toStdString()));
                        return;
                    }

                    auto& apvts    = processor.getValueTreeState();
                    auto& parts    = processor.getPartManager();
                    auto& browser  = processor.getPatchBrowser();

                    auto writeParam = [&apvts] (const juce::String& id, float v)
                    {
                        if (auto* p = apvts.getParameter (id))
                            p->setValueNotifyingHost (p->convertTo0to1 (v));
                    };

                    // Clear every slot — bank import is a replace, not a merge.
                    for (int i = 0; i < PartManager::kNumRackFmSlots; ++i)
                    {
                        parts.setSlotActive ({ PartManager::InstrumentType::FM, i }, false);
                        browser.clearActivePatchPath (i);
                    }
                    for (int i = 0; i < PartManager::kNumRackSqSlots; ++i)
                        parts.setSlotActive ({ PartManager::InstrumentType::SQ, i }, false);
                    for (int i = 0; i < PartManager::kNumRackDSlots; ++i)
                        parts.setSlotActive ({ PartManager::InstrumentType::D,  i }, false);

                    static const std::array<const char*, SN76489Engine::kNumChannels>
                        kPsgIds { "ch1", "ch2", "ch3", "noise" };

                    juce::StringArray rowErrors;
                    int restored = 0;
                    for (const auto& row : bnk.rows)
                    {
                        PartManager::InstrumentType type;
                        if      (row.type == "fm") type = PartManager::InstrumentType::FM;
                        else if (row.type == "sq") type = PartManager::InstrumentType::SQ;
                        else if (row.type == "d")  type = PartManager::InstrumentType::D;
                        else
                        {
                            rowErrors.add ("unknown row type \"" + row.type + "\"");
                            continue;
                        }

                        if (row.slot < 0 || row.slot >= PartManager::slotPoolSize (type))
                        {
                            rowErrors.add ("row slot " + juce::String (row.slot) + " out of range");
                            continue;
                        }

                        const PartManager::SlotId slot { type, row.slot };
                        parts.setSlotActive (slot, true);

                        juce::String suffix;
                        if (type == PartManager::InstrumentType::FM)
                            suffix = "_part" + juce::String (row.slot + 1);
                        else if (type == PartManager::InstrumentType::SQ)
                            suffix = juce::String ("_psg_") + kPsgIds[(std::size_t) row.slot];
                        else
                            suffix = "_dac";

                        if (type == PartManager::InstrumentType::FM
                            && row.patchPath.isNotEmpty())
                        {
                            const auto loadErr = browser.loadIntoPart (row.slot, row.patchPath);
                            if (! loadErr.empty())
                                rowErrors.add ("part " + juce::String (row.slot + 1) + ": "
                                               + juce::String (loadErr));
                        }

                        writeParam ("midi_ch"       + suffix, (float) row.midiCh);
                        writeParam ("transpose_st"  + suffix, (float) row.transposeSt);
                        writeParam ("transpose_oct" + suffix, (float) row.transposeOct);
                        writeParam ("note_lo"       + suffix, (float) row.noteLo);
                        writeParam ("note_hi"       + suffix, (float) row.noteHi);
                        writeParam ("detune_cents"  + suffix, (float) row.detuneCents);
                        writeParam ("balance"       + suffix, row.balance);

                        ++restored;
                    }

                    // Refresh the JS rack widget so the rows appear visually.
                    emitPatchRootsChanged();

                    if (restored > 0)
                        emitNotify ("info",
                            "Imported " + juce::String (restored)
                                + (restored == 1 ? " row from " : " rows from ")
                                + fileName);
                    if (! rowErrors.isEmpty())
                        emitNotify ("warn", rowErrors.joinIntoString ("; "));
                    if (restored == 0 && rowErrors.isEmpty())
                        emitNotify ("warn", "Bank file contained no rows.");

                    auto* obj = new juce::DynamicObject();
                    obj->setProperty ("ok",         restored > 0 || bnk.rows.empty());
                    obj->setProperty ("savedCount", restored);
                    if (! rowErrors.isEmpty())
                        obj->setProperty ("error", rowErrors.joinIntoString ("; "));
                    completion (juce::var (obj));
                });
        });

    // Export Bank dialog (Task 24) — snapshot every active rack row + its
    // per-instrument routing into a JSON bundle the user picks. Empty rack
    // surfaces a toast and aborts (per task spec verification step). The
    // patchPath stored per FM row is whatever PatchBrowser::activePatchPath
    // currently holds for that part, so importing on a different machine
    // requires that path to resolve there too — the same cross-OS caveat as
    // the rest of the patch system (04-patch-system.md).
    options = options.withNativeFunction ("exportBankDialog",
        [this] (const juce::Array<juce::var>&, Completion completion)
        {
            auto& apvts   = processor.getValueTreeState();
            auto& parts   = processor.getPartManager();
            auto& browser = processor.getPatchBrowser();

            auto readInt = [&apvts] (const juce::String& id, int dflt) -> int
            {
                if (auto* p = apvts.getRawParameterValue (id))
                    return juce::roundToInt (p->load());
                return dflt;
            };
            auto readFloat = [&apvts] (const juce::String& id, float dflt) -> float
            {
                if (auto* p = apvts.getRawParameterValue (id))
                    return p->load();
                return dflt;
            };

            genvst::bank::Bank bnk;
            auto addRow = [&] (const char*        typeStr,
                               int                slotIdx,
                               const juce::String& suffix,
                               const juce::String& patchPath,
                               int                 defaultMidi)
            {
                genvst::bank::BankRow r;
                r.type         = typeStr;
                r.slot         = slotIdx;
                r.patchPath    = patchPath;
                r.midiCh       = readInt   ("midi_ch"       + suffix, defaultMidi);
                r.transposeSt  = readInt   ("transpose_st"  + suffix, 0);
                r.transposeOct = readInt   ("transpose_oct" + suffix, 0);
                r.noteLo       = readInt   ("note_lo"       + suffix, 0);
                r.noteHi       = readInt   ("note_hi"       + suffix, 127);
                r.detuneCents  = readInt   ("detune_cents"  + suffix, 0);
                r.balance      = readFloat ("balance"       + suffix, 0.0f);
                bnk.rows.push_back (std::move (r));
            };

            static const std::array<const char*, SN76489Engine::kNumChannels>
                kPsgIds { "ch1", "ch2", "ch3", "noise" };
            static constexpr std::array<int, SN76489Engine::kNumChannels>
                kPsgDefCh { 11, 12, 13, 14 };

            for (int i = 0; i < PartManager::kNumRackFmSlots; ++i)
                if (parts.isSlotActive ({ PartManager::InstrumentType::FM, i }))
                    addRow ("fm", i,
                            "_part" + juce::String (i + 1),
                            browser.activePatchPath (i),
                            i + 1);

            for (int i = 0; i < PartManager::kNumRackSqSlots; ++i)
                if (parts.isSlotActive ({ PartManager::InstrumentType::SQ, i }))
                    addRow ("sq", i,
                            juce::String ("_psg_") + kPsgIds[(std::size_t) i],
                            {},
                            kPsgDefCh[(std::size_t) i]);

            for (int i = 0; i < PartManager::kNumRackDSlots; ++i)
                if (parts.isSlotActive ({ PartManager::InstrumentType::D, i }))
                    addRow ("d", i, "_dac", {}, 16);

            if (bnk.rows.empty())
            {
                emitNotify ("warn", "Rack is empty — nothing to export.");
                completion (makeStatusVar ("rack empty"));
                return;
            }

            bankExportChooser = std::make_unique<juce::FileChooser> (
                "Export Bank",
                juce::File{}.getChildFile ("bank.gnbank"),
                "*.gnbank;*.json");

            const auto flags = juce::FileBrowserComponent::saveMode
                             | juce::FileBrowserComponent::canSelectFiles
                             | juce::FileBrowserComponent::warnAboutOverwriting;

            bankExportChooser->launchAsync (flags,
                [this, completion = std::move (completion), bnk = std::move (bnk)]
                (const juce::FileChooser& fc) mutable
                {
                    const juce::File file = fc.getResult();
                    if (file == juce::File{})
                    { completion (makeStatusVar ({})); return; }   // cancelled

                    const auto err = genvst::bank::writeToFile (bnk, file);
                    if (err.isNotEmpty())
                    {
                        emitNotify ("error", err);
                        completion (makeStatusVar (err.toStdString()));
                        return;
                    }

                    emitNotify ("info",
                        "Exported " + juce::String ((int) bnk.rows.size())
                            + (bnk.rows.size() == 1 ? " row to " : " rows to ")
                            + file.getFileName());
                    completion (makeStatusVar ({}));
                });
        });

    // Save State dialog (Task 24) — wrap getStateInformation in a file
    // chooser. `.gnvst` is the new extension; the bytes are the same
    // AudioProcessorValueTreeState XML the plugin serialises through the
    // standard JUCE state path (Task 16), so a `.gnvst` file is identical
    // to the host's project-embedded state blob — just lifted to a
    // standalone file the user can copy across DAWs / sessions.
    options = options.withNativeFunction ("saveStateDialog",
        [this] (const juce::Array<juce::var>&, Completion completion)
        {
            stateSaveChooser = std::make_unique<juce::FileChooser> (
                "Save State",
                juce::File{}.getChildFile ("state.gnvst"),
                "*.gnvst");

            const auto flags = juce::FileBrowserComponent::saveMode
                             | juce::FileBrowserComponent::canSelectFiles
                             | juce::FileBrowserComponent::warnAboutOverwriting;

            stateSaveChooser->launchAsync (flags,
                [this, completion = std::move (completion)]
                (const juce::FileChooser& fc) mutable
                {
                    const juce::File file = fc.getResult();
                    if (file == juce::File{})
                    { completion (makeStatusVar ({})); return; }   // cancelled

                    juce::MemoryBlock block;
                    processor.getStateInformation (block);
                    if (! file.replaceWithData (block.getData(), block.getSize()))
                    {
                        emitNotify ("error",
                            "Could not write state file: " + file.getFullPathName());
                        completion (makeStatusVar ("write failed"));
                        return;
                    }
                    emitNotify ("info", "Saved state to " + file.getFileName());
                    completion (makeStatusVar ({}));
                });
        });

    // Load State dialog (Task 24) — read `.gnvst` bytes and replay through
    // setStateInformation. Restores the full apvts + routing + DAC PCM +
    // custom roots + rack + UI state (everything Task 16 persists).
    options = options.withNativeFunction ("loadStateDialog",
        [this] (const juce::Array<juce::var>&, Completion completion)
        {
            stateLoadChooser = std::make_unique<juce::FileChooser> (
                "Load State", juce::File{}, "*.gnvst");

            const auto flags = juce::FileBrowserComponent::openMode
                             | juce::FileBrowserComponent::canSelectFiles;

            stateLoadChooser->launchAsync (flags,
                [this, completion = std::move (completion)]
                (const juce::FileChooser& fc) mutable
                {
                    const juce::File file = fc.getResult();
                    if (file == juce::File{})
                    { completion (makeStatusVar ({})); return; }   // cancelled

                    juce::MemoryBlock block;
                    if (! file.loadFileAsData (block) || block.getSize() == 0)
                    {
                        emitNotify ("error",
                            "Could not read state file: " + file.getFileName());
                        completion (makeStatusVar ("read failed"));
                        return;
                    }
                    processor.setStateInformation (block.getData(),
                                                   (int) block.getSize());
                    // Push the UI refresh — state restore replays patches +
                    // routing + custom roots, so the rack widget + lists need
                    // to refetch.
                    emitPatchRootsChanged();
                    emitNotify ("info",
                        "Loaded state from " + file.getFileName());
                    completion (makeStatusVar ({}));
                });
        });

    // Import / Export Instrument (Task 24). Thin aliases for the existing
    // patch-browser file-chooser paths so the IMPORT-tab JS calls a uniform
    // set of fns. importFileDialog / exportFileDialog stay registered for
    // the patch-browser modal's buttons; these duplicate their behaviour
    // under names that match the IMPORT-tab button labels.
    options = options.withNativeFunction ("importInstrumentDialog",
        [this] (const juce::Array<juce::var>&, Completion completion)
        {
            importChooser = std::make_unique<juce::FileChooser> (
                "Import Instrument", juce::File{},
                juce::String (buildPatchExtensionFilter()));

            const auto flags = juce::FileBrowserComponent::openMode
                             | juce::FileBrowserComponent::canSelectFiles;

            importChooser->launchAsync (flags,
                [this, completion = std::move (completion)]
                (const juce::FileChooser& fc) mutable
                {
                    const juce::File file = fc.getResult();
                    if (file == juce::File{})
                    { completion (makeStatusVar ({})); return; }   // cancelled

                    const auto err = processor.getPatchBrowser()
                                         .importPatchFile (file.getFullPathName());
                    if (! err.empty())
                        emitNotify ("error", juce::String (err));
                    else
                    {
                        emitPatchRootsChanged();
                        emitNotify ("info",
                            "Imported instrument: " + file.getFileName());
                    }
                    completion (makeStatusVar (err));
                });
        });

    options = options.withNativeFunction ("exportInstrumentDialog",
        [this] (const juce::Array<juce::var>&, Completion completion)
        {
            exportChooser = std::make_unique<juce::FileChooser> (
                "Export Instrument",
                juce::File{}.getChildFile ("instrument.tfi"),
                "*.tfi;*.vgi");

            const auto flags = juce::FileBrowserComponent::saveMode
                             | juce::FileBrowserComponent::canSelectFiles
                             | juce::FileBrowserComponent::warnAboutOverwriting;

            exportChooser->launchAsync (flags,
                [this, completion = std::move (completion)]
                (const juce::FileChooser& fc) mutable
                {
                    const juce::File file = fc.getResult();
                    if (file == juce::File{})
                    { completion (makeStatusVar ({})); return; }   // cancelled

                    Patch current;
                    processor.readLivePatch (selectedPart, current);
                    const auto err = processor.getPatchBrowser()
                                         .exportPatchToPath (current,
                                                             file.getFullPathName());
                    if (! err.empty())
                        emitNotify ("error", juce::String (err));
                    else
                        emitNotify ("info",
                            "Exported instrument to " + file.getFileName());
                    completion (makeStatusVar (err));
                });
        });

    // Log VGM (Task 29). Toggle starts / stops a .vgm capture under
    // <userAppData>/GenVst/logs/. On start the file path goes back to JS so
    // the IMPORT-tab button can flip its label to "STOP LOG"; on stop the
    // file path is surfaced via toast and returned so the JS can flip back to
    // "LOG VGM". The audio thread is hooked at Voice / SN76489Engine, so
    // every chip register write reaches the open file via the SPSC ring + the
    // logger's 10 Hz flush timer.
    options = options.withNativeFunction ("toggleVgmLogging",
        [this] (const juce::Array<juce::var>&, Completion completion)
        {
            juce::String pathOrError;
            const bool nowActive = processor.getVgmLogger().toggle (pathOrError);

            auto* obj = new juce::DynamicObject();
            obj->setProperty ("ok",     true);
            obj->setProperty ("active", nowActive);
            obj->setProperty ("path",   pathOrError);

            if (nowActive)
            {
                emitNotify ("info",
                    "VGM logging started — " + pathOrError);
            }
            else if (pathOrError.isNotEmpty())
            {
                emitNotify ("info",
                    "VGM logged to " + pathOrError);
            }
            else
            {
                emitNotify ("error",
                    "VGM logging stopped (no file written).");
            }
            completion (juce::var (obj));
        });

    // Import Tuning (Task 30) — open a *.scl file picker, parse, swap table,
    // emit a toast with the scale description.
    options = options.withNativeFunction ("importTuningDialog",
        [this] (const juce::Array<juce::var>&, Completion completion)
        {
            sclChooser = std::make_unique<juce::FileChooser> (
                "Import Scala Tuning", juce::File{}, "*.scl");

            const auto flags = juce::FileBrowserComponent::openMode
                             | juce::FileBrowserComponent::canSelectFiles;

            sclChooser->launchAsync (flags,
                [this, completion = std::move (completion)] (const juce::FileChooser& fc) mutable
                {
                    const juce::File file = fc.getResult();
                    if (file == juce::File{})
                    { completion (makeStatusVar ({})); return; }   // cancelled

                    juce::String parseError;
                    auto table = parseScl (file.getFullPathName(), parseError);

                    if (table == nullptr)
                    {
                        emitNotify ("error", "Tuning import failed: " + parseError);
                        completion (makeStatusVar (parseError.toStdString()));
                        return;
                    }

                    Tuning::instance().setTable (table, file.getFullPathName());
                    emitNotify ("info", "Tuning loaded: " + table->description);

                    auto* obj = new juce::DynamicObject();
                    obj->setProperty ("ok",          true);
                    obj->setProperty ("description", table->description);
                    completion (juce::var (obj));
                });
        });

    // Reset tuning to 12-TET (used by the Settings modal "Reset to 12-TET" item).
    options = options.withNativeFunction ("resetTuningToDefault",
        [this] (const juce::Array<juce::var>&, Completion completion)
        {
            Tuning::instance().resetToDefault();
            emitNotify ("info", "Tuning reset to 12-TET.");
            completion (makeStatusVar ({}));
        });

    // Export file dialog — save file; extension picks TFI vs VGI.
    options = options.withNativeFunction ("exportFileDialog",
        [this] (const juce::Array<juce::var>& args, Completion completion)
        {
            // `format` arg (optional) picks the default extension; the user
            // can still pick either via the OS dialog.
            const juce::String format = (! args.isEmpty() && args[0].isString())
                                            ? args[0].toString().toLowerCase()
                                            : juce::String ("tfi");
            const juce::String suggested = juce::String ("patch.")
                                         + (format == "vgi" ? "vgi" : "tfi");

            exportChooser = std::make_unique<juce::FileChooser> (
                "Export patch", juce::File{}.getChildFile (suggested),
                "*.tfi;*.vgi");

            const auto flags = juce::FileBrowserComponent::saveMode
                             | juce::FileBrowserComponent::canSelectFiles
                             | juce::FileBrowserComponent::warnAboutOverwriting;

            exportChooser->launchAsync (flags,
                [this, completion = std::move (completion)] (const juce::FileChooser& fc) mutable
                {
                    const juce::File file = fc.getResult();
                    if (file == juce::File{})
                    { completion (makeStatusVar ({})); return; }   // cancelled

                    Patch current;
                    processor.readLivePatch (selectedPart, current);
                    const auto err = processor.getPatchBrowser()
                                         .exportPatchToPath (current, file.getFullPathName());
                    if (! err.empty())
                        emitNotify ("error", juce::String (err));
                    completion (makeStatusVar (err));
                });
        });

    // Add folder dialog — picks a directory and registers it as a custom root.
    options = options.withNativeFunction ("addFolderDialog",
        [this] (const juce::Array<juce::var>&, Completion completion)
        {
            folderChooser = std::make_unique<juce::FileChooser> (
                "Add patch folder", juce::File{}, juce::String());

            const auto flags = juce::FileBrowserComponent::openMode
                             | juce::FileBrowserComponent::canSelectDirectories;

            folderChooser->launchAsync (flags,
                [this, completion = std::move (completion)] (const juce::FileChooser& fc) mutable
                {
                    const juce::File dir = fc.getResult();
                    if (dir == juce::File{})
                    { completion (makeStatusVar ({})); return; }   // cancelled

                    const auto id = processor.getPatchBrowser()
                                        .addCustomRoot (dir.getFullPathName());
                    if (id.isEmpty())
                    {
                        emitNotify ("error",
                            "Could not register folder: " + dir.getFullPathName());
                        completion (makeStatusVar ("could not register folder"));
                        return;
                    }
                    emitPatchRootsChanged();
                    auto* obj = new juce::DynamicObject();
                    obj->setProperty ("ok", true);
                    obj->setProperty ("id", id);
                    completion (juce::var (obj));
                });
        });

    // Preview — synthetic middle-C note-on at the given velocity for ~1s on
    // the currently selected FM part. The release is fired by an editor-side
    // juce::Timer so the JS side can fire-and-forget; a second Preview click
    // before the 1s lapses retriggers the timer.
    options = options.withNativeFunction ("previewPatch",
        [this] (const juce::Array<juce::var>& args, Completion completion)
        {
            constexpr int kPreviewNote     = 60;     // middle C
            constexpr int kPreviewVelocity = 100;
            const int durationMs = (! args.isEmpty() && args[0].isInt())
                                       ? juce::jlimit (50, 5000, (int) args[0])
                                       : 1000;

            // If a previous preview is still ringing on a different part, cut
            // it before we start a fresh one — keeps voices free.
            if (previewActivePart >= 0 && previewActiveNote >= 0)
                processor.queuePreviewNoteOff (previewActivePart, previewActiveNote);

            previewActivePart = selectedPart;
            previewActiveNote = kPreviewNote;
            processor.queuePreviewNoteOn (previewActivePart, previewActiveNote,
                                          kPreviewVelocity);

            // (Re)arm the release timer for `durationMs`.
            class ReleaseTimer : public juce::Timer
            {
            public:
                ReleaseTimer (GenVstAudioProcessorEditor& ed) : editor (ed) {}
                void timerCallback() override
                {
                    stopTimer();
                    if (editor.previewActivePart >= 0 && editor.previewActiveNote >= 0)
                        editor.processor.queuePreviewNoteOff (editor.previewActivePart,
                                                              editor.previewActiveNote);
                    editor.previewActivePart = -1;
                    editor.previewActiveNote = -1;
                }
            private:
                GenVstAudioProcessorEditor& editor;
            };

            if (previewReleaseTimer == nullptr)
                previewReleaseTimer = std::make_unique<ReleaseTimer> (*this);
            previewReleaseTimer->stopTimer();
            previewReleaseTimer->startTimer (durationMs);

            auto* obj = new juce::DynamicObject();
            obj->setProperty ("ok",   true);
            obj->setProperty ("part", selectedPart);
            completion (juce::var (obj));
        });

    // --- Channel paging ------------------------------------------------------
    options = options.withNativeFunction ("selectChannel",
        [this] (const juce::Array<juce::var>& args, Completion completion)
        {
            if (args.isEmpty() || ! args[0].isInt())
            { completion (makeStatusVar ("part index required")); return; }
            const int n = juce::jlimit (0, PartManager::kNumParts - 1, (int) args[0]);
            selectedPart = n;
            processor.setUiSelectedPart (n);  // persisted across project save/load
            rebuildFmAttachments (n);
            auto* obj = new juce::DynamicObject();
            obj->setProperty ("ok",   true);
            obj->setProperty ("part", n);
            completion (juce::var (obj));
        });

    // --- Task 22 — Rack native functions -------------------------------------
    // The instrument rack (08-ui-views.md view 1 revised) drives these:
    //   getRackState           snapshot every active slot for the rack list
    //   selectPart             pick (type, slotIndex) -> sets section + paging
    //   clearPart              remove a row -> deactivates slot, clears patch
    //   addInstrument          requests an empty slot of a given type
    //
    // Slot types map to the engine as:
    //   FM   -> partManager FM slots 0..4 -> FM parts 0..4
    //   SQ   -> 0..2 PSG tones, 3 = PSG noise
    //   D    -> the single DAC slot

    auto rackSlotTypeFromString = [] (const juce::String& s) -> std::optional<PartManager::InstrumentType>
    {
        if (s.equalsIgnoreCase ("fm"))  return PartManager::InstrumentType::FM;
        if (s.equalsIgnoreCase ("sq"))  return PartManager::InstrumentType::SQ;
        if (s.equalsIgnoreCase ("d"))   return PartManager::InstrumentType::D;
        if (s.equalsIgnoreCase ("dac")) return PartManager::InstrumentType::D;
        return std::nullopt;
    };

    auto rackSlotTypeToString = [] (PartManager::InstrumentType t) -> juce::String
    {
        switch (t)
        {
            case PartManager::InstrumentType::FM: return "fm";
            case PartManager::InstrumentType::SQ: return "sq";
            case PartManager::InstrumentType::D:  return "d";
        }
        return "fm";
    };

    options = options.withNativeFunction ("getRackState",
        [this, rackSlotTypeToString]
        (const juce::Array<juce::var>&, Completion completion)
        {
            auto& apvts   = processor.getValueTreeState();
            auto& parts   = processor.getPartManager();
            auto& browser = processor.getPatchBrowser();
            auto& kit     = processor.getDacKit();

            auto readInt = [&apvts] (const juce::String& id, int dflt) -> int
            {
                if (auto* p = apvts.getRawParameterValue (id))
                    return juce::roundToInt (p->load());
                return dflt;
            };
            auto readFloat = [&apvts] (const juce::String& id, float dflt) -> float
            {
                if (auto* p = apvts.getRawParameterValue (id))
                    return p->load();
                return dflt;
            };

            juce::Array<juce::var> rows;
            auto pushRow = [&] (PartManager::SlotId slot,
                                const juce::String& suffix,
                                const juce::String& patchName,
                                int defaultMidi)
            {
                auto* obj = new juce::DynamicObject();
                obj->setProperty ("type",         rackSlotTypeToString (slot.type));
                obj->setProperty ("slotIndex",    slot.index);
                obj->setProperty ("paramSuffix",  suffix);
                obj->setProperty ("patchName",    patchName);
                obj->setProperty ("midiCh",       readInt   ("midi_ch"       + suffix, defaultMidi));
                obj->setProperty ("transposeSt",  readInt   ("transpose_st"  + suffix, 0));
                obj->setProperty ("transposeOct", readInt   ("transpose_oct" + suffix, 0));
                obj->setProperty ("noteLo",       readInt   ("note_lo"       + suffix, 0));
                obj->setProperty ("noteHi",       readInt   ("note_hi"       + suffix, 127));
                obj->setProperty ("detuneCents",  readInt   ("detune_cents"  + suffix, 0));
                obj->setProperty ("balance",      readFloat ("balance"       + suffix, 0.0f));
                rows.add (juce::var (obj));
            };

            // Task 27 — enumerate in user-defined order (PartManager::rackOrder)
            // rather than per-pool index. Selection + drag-drop reorder rides
            // on this sequence.
            static const std::array<const char*, SN76489Engine::kNumChannels> kPsgRackIds
                { "ch1", "ch2", "ch3", "noise" };
            static constexpr std::array<int, SN76489Engine::kNumChannels> kPsgDefaultCh
                { 11, 12, 13, 14 };
            static const std::array<const char*, SN76489Engine::kNumChannels> kPsgRackLabel
                { "PSG 1", "PSG 2", "PSG 3", "PSG Noise" };

            for (const auto& slot : parts.getRackOrder())
            {
                if (! parts.isSlotActive (slot)) continue;   // defensive
                switch (slot.type)
                {
                    case PartManager::InstrumentType::FM:
                    {
                        if (slot.index < 0 || slot.index >= PartManager::kNumRackFmSlots) continue;
                        const auto path = browser.activePatchPath (slot.index);
                        // Fall back to the PartManager-cached patch.name when
                        // no patch file is bound (Task 33 — a pasted slot is
                        // by-value, so it has no source path but it does have
                        // a patch.name carried over in the clipboard payload).
                        juce::String name;
                        if (path.isNotEmpty())
                            name = juce::File (path).getFileNameWithoutExtension();
                        else
                        {
                            const auto& pname = parts.getPatch (slot.index).name;
                            name = pname.empty() ? juce::String ("— empty —")
                                                 : juce::String (pname);
                        }
                        pushRow (slot, "_part" + juce::String (slot.index + 1), name, slot.index + 1);
                        break;
                    }
                    case PartManager::InstrumentType::SQ:
                    {
                        if (slot.index < 0 || slot.index >= PartManager::kNumRackSqSlots) continue;
                        pushRow (slot,
                                 juce::String ("_psg_") + kPsgRackIds[(std::size_t) slot.index],
                                 juce::String (kPsgRackLabel[(std::size_t) slot.index]),
                                 kPsgDefaultCh[(std::size_t) slot.index]);
                        break;
                    }
                    case PartManager::InstrumentType::D:
                    {
                        if (slot.index < 0 || slot.index >= PartManager::kNumRackDSlots) continue;
                        // Task 31 — the rack row's display name now summarises
                        // how many cells of the kit are loaded; no single
                        // sample name applies to the kit as a whole.
                        int loaded = 0;
                        for (int i = 0; i < DACKit::kNumCells; ++i)
                            if (kit.hasCell (i)) ++loaded;
                        const juce::String name = loaded == 0
                            ? juce::String ("— no samples —")
                            : (juce::String ("Kit (") + juce::String (loaded)
                                  + (loaded == 1 ? " sample)" : " samples)"));
                        pushRow (slot, "_dac", name, 16);
                        break;
                    }
                }
            }

            auto* outObj = new juce::DynamicObject();
            outObj->setProperty ("ok",   true);
            outObj->setProperty ("rows", juce::var (rows));
            // Pool sizes the UI uses to clamp the + popover / empty-slot logic.
            auto* pool = new juce::DynamicObject();
            pool->setProperty ("fm", PartManager::kNumRackFmSlots);
            pool->setProperty ("sq", PartManager::kNumRackSqSlots);
            pool->setProperty ("d",  PartManager::kNumRackDSlots);
            outObj->setProperty ("pool", juce::var (pool));
            completion (juce::var (outObj));
        });

    options = options.withNativeFunction ("selectPart",
        [this, rackSlotTypeFromString]
        (const juce::Array<juce::var>& args, Completion completion)
        {
            if (args.size() < 2 || ! args[0].isString())
            { completion (makeStatusVar ("type, slotIndex required")); return; }
            const auto type = rackSlotTypeFromString (args[0].toString());
            if (! type.has_value())
            { completion (makeStatusVar ("invalid type")); return; }
            const int slotIndex = (int) args[1];

            // FM rows page the FM panel via the existing rebuildFmAttachments
            // path. SQ / D rows simply tell the bottom region which section
            // to display — the section pill is implied by the row click.
            juce::String section;
            switch (*type)
            {
                case PartManager::InstrumentType::FM:
                {
                    const int n = juce::jlimit (0, PartManager::kNumParts - 1, slotIndex);
                    selectedPart = n;
                    processor.setUiSelectedPart (n);
                    rebuildFmAttachments (n);
                    section = "FM";
                    break;
                }
                case PartManager::InstrumentType::SQ: section = "SQ"; break;
                case PartManager::InstrumentType::D:  section = "D";  break;
            }

            auto* obj = new juce::DynamicObject();
            obj->setProperty ("ok",        true);
            obj->setProperty ("type",      args[0].toString());
            obj->setProperty ("slotIndex", slotIndex);
            obj->setProperty ("section",   section);
            completion (juce::var (obj));
        });

    options = options.withNativeFunction ("clearPart",
        [this, rackSlotTypeFromString]
        (const juce::Array<juce::var>& args, Completion completion)
        {
            if (args.size() < 2 || ! args[0].isString())
            { completion (makeStatusVar ("type, slotIndex required")); return; }
            const auto type = rackSlotTypeFromString (args[0].toString());
            if (! type.has_value())
            { completion (makeStatusVar ("invalid type")); return; }
            const int slotIndex = (int) args[1];
            const PartManager::SlotId slot { *type, slotIndex };
            auto& parts = processor.getPartManager();

            // Clear any patch / WAV bound to this slot, then deactivate it.
            switch (*type)
            {
                case PartManager::InstrumentType::FM:
                {
                    if (slotIndex >= 0 && slotIndex < PartManager::kNumParts)
                        processor.getPatchBrowser().clearActivePatchPath (slotIndex);
                    break;
                }
                case PartManager::InstrumentType::SQ:
                    // PSG channels keep their apvts state — clearing the row
                    // is the user signalling "this slot is empty"; volumes /
                    // pans stay so re-adding the row brings back the same
                    // settings (Genny parity).
                    break;
                case PartManager::InstrumentType::D:
                    // Task 31 — wipe every cell in the kit when the user
                    // removes the DAC rack row. Re-adding the row brings a
                    // fresh empty grid.
                    processor.getDacKit().clearAll();
                    break;
            }
            parts.setSlotActive (slot, false);

            completion (makeStatusVar ({}));
        });

    options = options.withNativeFunction ("addInstrument",
        [this, rackSlotTypeFromString]
        (const juce::Array<juce::var>& args, Completion completion)
        {
            if (args.isEmpty() || ! args[0].isString())
            { completion (makeStatusVar ("type required")); return; }
            const auto type = rackSlotTypeFromString (args[0].toString());
            if (! type.has_value())
            { completion (makeStatusVar ("invalid type")); return; }
            auto& parts = processor.getPartManager();
            const auto free = parts.getFreeSlot (*type);
            auto* obj = new juce::DynamicObject();
            if (! free.has_value())
            {
                obj->setProperty ("ok",    false);
                obj->setProperty ("error", "All slots of this type are in use.");
                completion (juce::var (obj));
                return;
            }
            // Mark the slot active. The JS side then opens the patch browser
            // (scoped to the requested type) and calls loadInstrument with the
            // returned slotIndex (re-routed via setSelectedPartForSlot below).
            parts.setSlotActive (*free, true);

            // If this is an FM slot, also page the editor's FM attachments to
            // it so the bottom panel + right column rebind immediately.
            if (free->type == PartManager::InstrumentType::FM)
            {
                selectedPart = free->index;
                processor.setUiSelectedPart (free->index);
                rebuildFmAttachments (free->index);
            }

            obj->setProperty ("ok",        true);
            obj->setProperty ("type",      args[0].toString());
            obj->setProperty ("slotIndex", free->index);
            completion (juce::var (obj));
        });

    // Task 27 — Move a rack row from one position to another within the
    // user-defined ordering. Indices refer to rows in PartManager::rackOrder
    // (i.e. visible row indices in the rack widget, ignoring the trailing
    // "+ ADD INSTRUMENT" cell). JS calls this on pointerup of a drag, then
    // re-runs getRackState to repaint from the new sequence.
    options = options.withNativeFunction ("reorderRackRow",
        [this] (const juce::Array<juce::var>& args, Completion completion)
        {
            if (args.size() < 2 || ! args[0].isInt() || ! args[1].isInt())
            { completion (makeStatusVar ("fromIndex, toIndex required")); return; }
            const int from = (int) args[0];
            const int to   = (int) args[1];
            processor.getPartManager().reorderSlot (from, to);
            completion (makeStatusVar ({}));
        });

    // Task 33 — Per-slot copy/paste.
    //
    // copySlot snapshots every parameter (Patch / routing / polyphony / PSG
    // envelope / DAC cells) that defines a rack row's identity into a juce::var
    // payload the JS side stores opaquely. pasteSlot replays that payload onto
    // a different slot of the *same type*: cross-type pastes are rejected at
    // the boundary (the rack widget also hides the paste glyph for
    // incompatible rows, but this is the authoritative guard).
    //
    // The clipboard lives JS-side: a paste needs the full payload round-tripped
    // back through args, so closing + reopening the editor naturally wipes the
    // clipboard (per task spec "editor-session only").
    //
    // Indices: like reorderRackRow above, the rowIndex refers to a position in
    // PartManager::rackOrder. Resolving rowIndex → SlotId at call time means
    // copy and paste both follow the user-visible row order even if the
    // backend slot pool indices look different (e.g. PSG noise is SQ slot 3).
    options = options.withNativeFunction ("copySlot",
        [this, rackSlotTypeToString] (const juce::Array<juce::var>& args, Completion completion)
        {
            if (args.isEmpty() || ! args[0].isInt())
            { completion (makeStatusVar ("rowIndex required")); return; }
            const int rowIdx = (int) args[0];
            auto& parts = processor.getPartManager();
            const auto& order = parts.getRackOrder();
            if (rowIdx < 0 || rowIdx >= (int) order.size())
            { completion (makeStatusVar ("rowIndex out of range")); return; }

            const auto slot   = order[(std::size_t) rowIdx];
            const auto suffix = suffixForSlot (slot);
            auto& apvts       = processor.getValueTreeState();

            auto* payload = new juce::DynamicObject();
            payload->setProperty ("type",    rackSlotTypeToString (slot.type));
            payload->setProperty ("routing", snapshotParams (apvts, suffix, routingParamIds()));

            switch (slot.type)
            {
                case PartManager::InstrumentType::FM:
                {
                    Patch live;
                    processor.readLivePatch (slot.index, live);
                    // PartManager carries the last-loaded patch.name; the live
                    // apvts snapshot has no name field, so fold it back in.
                    live.name = parts.getPatch (slot.index).name;
                    payload->setProperty ("patch",     patchToVar (live));
                    payload->setProperty ("patchPath", processor.getPatchBrowser()
                                                            .activePatchPath (slot.index));
                    payload->setProperty ("polyphony",
                        snapshotParams (apvts, suffix, fmPolyphonyParamIds()));
                    break;
                }
                case PartManager::InstrumentType::SQ:
                {
                    payload->setProperty ("psg",
                        snapshotParams (apvts, suffix, psgChannelParamIds()));
                    // Tone channels expose glide_time_psg_<ch>; noise has none.
                    if (slot.index < SN76489Engine::kNumToneChs)
                    {
                        const juce::String gid = "glide_time_psg_"
                            + juce::String (psgSubIds()[(std::size_t) slot.index]);
                        if (auto* p = apvts.getRawParameterValue (gid))
                            payload->setProperty ("psgGlide", (double) p->load());
                    }
                    break;
                }
                case PartManager::InstrumentType::D:
                {
                    auto& kit = processor.getDacKit();
                    juce::Array<juce::var> cells;
                    for (int i = 0; i < DACKit::kNumCells; ++i)
                    {
                        if (! kit.hasCell (i)) continue;
                        const auto* c = kit.cellPtr (i);
                        if (c == nullptr || c->mtPcm.empty()) continue;

                        auto* cellObj = new juce::DynamicObject();
                        cellObj->setProperty ("index", i);
                        cellObj->setProperty ("rate",  c->mtRate);
                        cellObj->setProperty ("name",  c->name);

                        juce::MemoryOutputStream b64;
                        juce::Base64::convertToBase64 (b64, c->mtPcm.data(), c->mtPcm.size());
                        cellObj->setProperty ("pcm", b64.toString());

                        cells.add (juce::var (cellObj));
                    }
                    payload->setProperty ("cells", juce::var (cells));
                    break;
                }
            }

            auto* result = new juce::DynamicObject();
            result->setProperty ("ok",      true);
            result->setProperty ("type",    rackSlotTypeToString (slot.type));
            result->setProperty ("payload", juce::var (payload));
            completion (juce::var (result));
        });

    options = options.withNativeFunction ("pasteSlot",
        [this, rackSlotTypeToString] (const juce::Array<juce::var>& args, Completion completion)
        {
            if (args.size() < 2 || ! args[0].isInt())
            { completion (makeStatusVar ("rowIndex, payload required")); return; }
            const int rowIdx = (int) args[0];
            const juce::var payloadVar = args[1];
            auto* payload = payloadVar.getDynamicObject();
            if (payload == nullptr)
            { completion (makeStatusVar ("payload must be an object")); return; }

            auto& parts = processor.getPartManager();
            const auto& order = parts.getRackOrder();
            if (rowIdx < 0 || rowIdx >= (int) order.size())
            { completion (makeStatusVar ("rowIndex out of range")); return; }

            const auto slot = order[(std::size_t) rowIdx];
            const juce::String payloadType = payload->getProperty ("type").toString();
            const juce::String slotType    = rackSlotTypeToString (slot.type);
            if (payloadType != slotType)
            { completion (makeStatusVar ("type mismatch")); return; }

            const auto suffix = suffixForSlot (slot);
            auto& apvts       = processor.getValueTreeState();

            // Routing applies to every type. Order matters here only for the
            // user's perception of intermediate states — apvts writes are
            // synchronous on the message thread, so the audio thread sees one
            // coherent slot configuration on the next block regardless.
            if (payload->hasProperty ("routing"))
                applyParams (apvts, suffix, payload->getProperty ("routing"));

            switch (slot.type)
            {
                case PartManager::InstrumentType::FM:
                {
                    if (payload->hasProperty ("patch"))
                    {
                        const Patch p = varToPatch (payload->getProperty ("patch"));
                        // Replay each per-op + per-part FM param through
                        // setValueNotifyingHost (same path as writePatchToParams
                        // in PluginProcessor.cpp) so the DAW sees the changes
                        // for automation purposes. Also stash the patch in
                        // PartManager so the rack-row label can fall back to
                        // patch.name when activePatchPath is empty.
                        for (int op = 0; op < kNumOps; ++op)
                        {
                            auto setOp = [&] (const char* base, int v, int lo, int hi)
                            {
                                const auto id = juce::String (base) + "_op"
                                                + juce::String (op + 1) + suffix;
                                if (auto* prm = apvts.getParameter (id))
                                    prm->setValueNotifyingHost (
                                        prm->convertTo0to1 (
                                            (float) juce::jlimit (lo, hi, v)));
                            };
                            setOp ("dt",   p.dt[op],   0, 6);
                            setOp ("mul",  p.mul[op],  0, 15);
                            setOp ("tl",   p.tl[op],   0, 127);
                            setOp ("ks",   p.ks[op],   0, 3);
                            setOp ("ar",   p.ar[op],   0, 31);
                            setOp ("dr",   p.dr[op],   0, 31);
                            setOp ("sr",   p.sr[op],   0, 31);
                            setOp ("rr",   p.rr[op],   0, 15);
                            setOp ("sl",   p.sl[op],   0, 15);
                            setOp ("ssg",  p.ssg[op],  0, 15);
                            setOp ("amon", p.amon[op], 0, 1);
                        }
                        auto setPart = [&] (const char* base, int v, int lo, int hi)
                        {
                            const auto id = juce::String (base) + suffix;
                            if (auto* prm = apvts.getParameter (id))
                                prm->setValueNotifyingHost (
                                    prm->convertTo0to1 (
                                        (float) juce::jlimit (lo, hi, v)));
                        };
                        setPart ("alg",        p.alg,        0, 7);
                        setPart ("fb",         p.fb,         0, 7);
                        setPart ("ams",        p.ams,        0, 3);
                        setPart ("pms",        p.pms,        0, 7);
                        setPart ("lr",         p.lr,         0, 3);
                        setPart ("lfo_enable", p.lfo_enable, 0, 1);
                        setPart ("lfo_rate",   p.lfo_rate,   0, 7);

                        parts.loadPatch (slot.index, p);
                    }
                    if (payload->hasProperty ("polyphony"))
                        applyParams (apvts, suffix, payload->getProperty ("polyphony"));
                    // Clone is by-value: the pasted slot is no longer tied to
                    // the source patch file. Clearing activePatchPath lets the
                    // rack-row label fall back to patch.name (see getRackState).
                    processor.getPatchBrowser().clearActivePatchPath (slot.index);
                    break;
                }
                case PartManager::InstrumentType::SQ:
                {
                    if (payload->hasProperty ("psg"))
                        applyParams (apvts, suffix, payload->getProperty ("psg"));
                    if (payload->hasProperty ("psgGlide")
                        && slot.index < SN76489Engine::kNumToneChs)
                    {
                        const juce::String gid = "glide_time_psg_"
                            + juce::String (psgSubIds()[(std::size_t) slot.index]);
                        if (auto* prm = apvts.getParameter (gid))
                        {
                            const float v = (float) (double) payload->getProperty ("psgGlide");
                            prm->setValueNotifyingHost (prm->convertTo0to1 (v));
                        }
                    }
                    break;
                }
                case PartManager::InstrumentType::D:
                {
                    auto& kit = processor.getDacKit();
                    kit.clearAll();
                    if (payload->hasProperty ("cells"))
                    {
                        const auto cellsVar = payload->getProperty ("cells");
                        if (auto* arr = cellsVar.getArray())
                        {
                            for (const auto& cv : *arr)
                            {
                                auto* cobj = cv.getDynamicObject();
                                if (cobj == nullptr) continue;
                                const int idx = (int) cobj->getProperty ("index");
                                if (idx < 0 || idx >= DACKit::kNumCells) continue;
                                const auto pcmStr = cobj->getProperty ("pcm").toString();
                                if (pcmStr.isEmpty()) continue;
                                juce::MemoryOutputStream decoded;
                                if (! juce::Base64::convertFromBase64 (decoded, pcmStr))
                                    continue;
                                const int rate = (int) cobj->getProperty ("rate");
                                const auto name = cobj->getProperty ("name").toString();
                                kit.loadCellRawPcm (idx,
                                    static_cast<const std::uint8_t*> (decoded.getData()),
                                    decoded.getDataSize(), rate, name);
                            }
                        }
                    }
                    break;
                }
            }

            // Re-page FM attachments so the bottom panel + operator widgets
            // repaint against the now-modified part. The apvts writes above
            // already drove every relay, but the FM widgets are bound to the
            // *selected* part — if the user pasted into the currently selected
            // FM slot, rebind so they see the changes immediately.
            if (slot.type == PartManager::InstrumentType::FM
                && slot.index == selectedPart)
                rebuildFmAttachments (selectedPart);

            // Refresh the rack so the row label updates (FM patch name) and
            // every downstream listener (e.g. the routing strip) re-resolves.
            emitPatchRootsChanged();

            auto* result = new juce::DynamicObject();
            result->setProperty ("ok",   true);
            result->setProperty ("type", slotType);
            completion (juce::var (result));
        });

    // --- Editor UI state (persisted across DAW project save/load) ------------
    // getInitialUiState: read once by the JS view at mount time. Returns the
    // values restored from the plugin XML state so the UI lands on the same
    // FM part + preset/import tab the user was on when the project was saved.
    options = options.withNativeFunction ("getInitialUiState",
        [this] (const juce::Array<juce::var>&, Completion completion)
        {
            auto* obj = new juce::DynamicObject();
            obj->setProperty ("ok",           true);
            obj->setProperty ("selectedPart", processor.uiSelectedPart());
            obj->setProperty ("presetTab",    processor.uiPresetTab());
            completion (juce::var (obj));
        });

    // setPresetTab: called by JS when the PRESETS / IMPORT tabs are toggled
    // so the choice survives a project save. Mirror to the processor's
    // uiPresetTabIndex which getStateInformation persists.
    options = options.withNativeFunction ("setPresetTab",
        [this] (const juce::Array<juce::var>& args, Completion completion)
        {
            if (args.isEmpty() || ! args[0].isInt())
            { completion (makeStatusVar ("tab index required")); return; }
            const int t = (int) args[0];
            processor.setUiPresetTab (t);
            completion (makeStatusVar ({}));
        });

    // --- Section switch (FM / SQ / D) ---------------------------------------
    // Records the section pill choice — the bottom-region content is swapped
    // by the JS view orchestrator (body[data-section]), so this function is
    // currently a no-op on the C++ side. Kept as a native function so future
    // C++-side section-switch work (e.g. selectively disabling FM telemetry
    // when SQ/D is visible) plugs in without a relay redesign.
    options = options.withNativeFunction ("selectSection",
        [] (const juce::Array<juce::var>& args, Completion completion)
        {
            juce::String section = (! args.isEmpty() && args[0].isString())
                                       ? args[0].toString() : juce::String ("FM");
            auto* obj = new juce::DynamicObject();
            obj->setProperty ("ok",      true);
            obj->setProperty ("section", section);
            completion (juce::var (obj));
        });

    // --- Routing (Task 13) ---------------------------------------------------
    // The routing modal + the inline MIDI step-fields on views 1/2/3 all
    // edit the same MidiRouter table via these three native functions
    // (08-ui-views.md view 5). Returns / accepts a destination-centric view:
    // fmParts[6] / psgTones[3] / psgNoise / dac, each holding a MIDI channel
    // (0=off, 1..16).
    options = options.withNativeFunction ("getRouting",
        [this] (const juce::Array<juce::var>&, Completion completion)
        {
            auto& router = processor.getMidiRouter();
            auto* obj = new juce::DynamicObject();

            juce::Array<juce::var> fm;
            for (int p = 0; p < PartManager::kNumParts; ++p)
                fm.add ((int) router.destinationChannel (
                            MidiRouter::destinationId ({ MidiRouter::Destination::Kind::FmPart, p })));
            obj->setProperty ("fmParts", fm);

            juce::Array<juce::var> tones;
            for (int t = 0; t < 3; ++t)
                tones.add ((int) router.destinationChannel (
                              MidiRouter::destinationId ({ MidiRouter::Destination::Kind::PsgTone, t })));
            obj->setProperty ("psgTones", tones);

            obj->setProperty ("psgNoise", (int) router.destinationChannel (
                                  MidiRouter::destinationId ({ MidiRouter::Destination::Kind::PsgNoise, 0 })));
            obj->setProperty ("dac",      (int) router.destinationChannel (
                                  MidiRouter::destinationId ({ MidiRouter::Destination::Kind::Dac, 0 })));

            completion (juce::var (obj));
        });

    options = options.withNativeFunction ("setRouting",
        [this] (const juce::Array<juce::var>& args, Completion completion)
        {
            if (args.size() < 3 || ! args[0].isString())
            { completion (makeStatusVar ("kind, index, channel required")); return; }
            const auto kind    = args[0].toString();
            const int  index   = (int) args[1];
            const int  channel = (int) args[2];
            const auto dest    = destinationFromKindIndex (kind, index);
            const int  destId  = MidiRouter::destinationId (dest);
            if (destId < 0) { completion (makeStatusVar ("invalid destination")); return; }
            processor.getMidiRouter().setDestinationChannel (destId, channel);
            completion (makeStatusVar ({}));
        });

    options = options.withNativeFunction ("resetRouting",
        [this] (const juce::Array<juce::var>&, Completion completion)
        {
            processor.getMidiRouter().resetRouting();
            completion (makeStatusVar ({}));
        });

    // --- Reset to defaults ---------------------------------------------------
    // Two scopes are exposed:
    //  - resetCurrentPart: snap every FM parameter for the currently selected
    //    part (operator + part + polyphony) back to its juce::AudioParameter
    //    default and clear the active patch path. The relays' valueChangedEvent
    //    then repaints every FM widget in one batch via the standard channel-
    //    paging contract (05-ui-ux.md "FM channel paging").
    //  - resetAllToDefaults: resets every per-part param across all 6 parts
    //    AND the global / PSG / DAC parameters PLUS routing. Used by the
    //    Settings modal's RESET ALL button.
    //
    // Both helpers walk the known apvts parameter IDs (PluginProcessor.cpp's
    // createParameterLayout). Looking up by ID rather than dynamic_casting
    // raw AudioProcessor::getParameters() keeps the dev-server gallery scratch
    // params out of the reset, and survives a JUCE-API change to private
    // members of RangedAudioParameter.
    auto resetParam = [this] (const juce::String& id)
    {
        if (auto* p = processor.getValueTreeState().getParameter (id))
            p->setValueNotifyingHost (p->getDefaultValue());
    };

    auto resetPartParams = [&resetParam] (int part)
    {
        for (int op = 0; op < kNumOps; ++op)
            for (int i = 0; i < kNumOpParams; ++i)
                resetParam (opParamIdForPart (kFmOpParamIds[(std::size_t) i], op, part));
        for (int i = 0; i < kNumPartParams; ++i)
            resetParam (partParamIdForPart (kFmPartParamIds[(std::size_t) i], part));
        const juce::String suffix = "_part" + juce::String (part + 1);
        resetParam ("poly_mode"    + suffix);
        resetParam ("mono_glide"   + suffix);
        resetParam ("unison_spread" + suffix);
    };

    options = options.withNativeFunction ("resetCurrentPart",
        [this, resetPartParams] (const juce::Array<juce::var>&, Completion completion)
        {
            resetPartParams (selectedPart);
            processor.getPatchBrowser().clearActivePatchPath (selectedPart);
            completion (makeStatusVar ({}));
        });

    options = options.withNativeFunction ("resetAllToDefaults",
        [this, resetParam, resetPartParams] (const juce::Array<juce::var>&, Completion completion)
        {
            // Per-part FM parameters across all 6 parts.
            for (int p = 0; p < PartManager::kNumParts; ++p)
                resetPartParams (p);

            // Global controls.
            resetParam ("master_gain");
            resetParam ("bend_range");
            resetParam ("vel_to_tl");
            resetParam ("true_stereo");
            resetParam ("aftertouch_target");
            resetParam ("voice_count");
            resetParam ("ui_scale");

            // PSG controls.
            const juce::StringArray psgIds { "ch1", "ch2", "ch3", "noise" };
            for (const auto& id : psgIds)
            {
                resetParam ("psg_vol_"  + id);
                resetParam ("psg_pan_"  + id);
                resetParam ("psg_bend_" + id);

                // Task 23 — per-channel envelope params.
                for (const auto* base : kPsgEnvSliderBases)
                    resetParam (juce::String (base) + "_" + id);
                resetParam ("psg_vel_" + id);
            }
            resetParam ("psg_noise_type");
            resetParam ("psg_noise_rate");
            resetParam ("psg_noise_auto");
            resetParam ("psg_mix");
            resetParam ("psg_layer");

            // DAC controls.
            resetParam ("dac_enable");
            resetParam ("dac_rate");
            resetParam ("dac_mode");
            resetParam ("dac_level");

            // Routing + DAC kit + active patch paths.
            processor.getMidiRouter().resetRouting();
            processor.getDacKit().clearAll();
            for (int p = 0; p < PartManager::kNumParts; ++p)
                processor.getPatchBrowser().clearActivePatchPath (p);

            completion (makeStatusVar ({}));
        });

    // --- Active patch path query ---------------------------------------------
    // The UI uses this after a selectChannel or a patchRootsChanged refresh to
    // figure out which pinned list (Instruments / Presets / Import) currently
    // holds the loaded patch and therefore which row should be highlighted.
    options = options.withNativeFunction ("getActivePatchPath",
        [this] (const juce::Array<juce::var>& args, Completion completion)
        {
            const int part = (! args.isEmpty() && args[0].isInt())
                                ? juce::jlimit (0, PartManager::kNumParts - 1,
                                                 (int) args[0])
                                : selectedPart;
            const auto path = processor.getPatchBrowser().activePatchPath (part);
            auto* obj = new juce::DynamicObject();
            obj->setProperty ("ok",   true);
            obj->setProperty ("path", path);
            obj->setProperty ("part", part);
            completion (juce::var (obj));
        });

    // --- DAC kit (Task 31 D view) -------------------------------------------
    // Per-cell load: the JS grid click passes the cell index; the chooser
    // result feeds DACKit::loadCellWav. Failure surfaces through emitNotify
    // and resolves to {ok:false}. The cell's stored rate defaults to the
    // current dac_rate apvts value at load time.
    options = options.withNativeFunction ("loadDacCellWav",
        [this] (const juce::Array<juce::var>& args, Completion completion)
        {
            if (args.isEmpty() || ! args[0].isInt())
            { completion (makeStatusVar ("cell index required")); return; }
            const int cellIdx = (int) args[0];
            if (cellIdx < 0 || cellIdx >= DACKit::kNumCells)
            { completion (makeStatusVar ("cell index out of range")); return; }

            wavChooser = std::make_unique<juce::FileChooser> (
                "Load WAV", juce::File{}, "*.wav");

            const auto flags = juce::FileBrowserComponent::openMode
                             | juce::FileBrowserComponent::canSelectFiles;

            wavChooser->launchAsync (flags,
                [this, cellIdx, completion = std::move (completion)]
                (const juce::FileChooser& fc) mutable
                {
                    const juce::File file = fc.getResult();
                    if (file == juce::File{})
                    {
                        completion (makeStatusVar ({}));   // cancelled
                        return;
                    }

                    // Default rate for this load: current dac_rate apvts
                    // value mapped through the 8000/11025/22050 table. Per
                    // Task 31 spec, dac_rate now means "default rate for the
                    // next load", not the global playback rate.
                    static constexpr int kRates[] { 8000, 11025, 22050 };
                    int rateIdx = 2;
                    if (auto* p = processor.getValueTreeState()
                                       .getRawParameterValue ("dac_rate"))
                        rateIdx = juce::jlimit (0, 2, juce::roundToInt (p->load()));
                    const int defaultRate = kRates[rateIdx];

                    if (! processor.getDacKit().loadCellWav (cellIdx, file, defaultRate))
                    {
                        emitNotify ("error", "Failed to load WAV: " + file.getFileName());
                        completion (makeStatusVar ("WAV load failed"));
                        return;
                    }

                    auto* obj = new juce::DynamicObject();
                    obj->setProperty ("ok",  true);
                    obj->setProperty ("cellIndex", cellIdx);
                    obj->setProperty ("kit", buildDacKitVar (processor.getDacKit()));
                    completion (juce::var (obj));
                });
        });

    options = options.withNativeFunction ("clearDacCell",
        [this] (const juce::Array<juce::var>& args, Completion completion)
        {
            if (args.isEmpty() || ! args[0].isInt())
            { completion (makeStatusVar ("cell index required")); return; }
            const int cellIdx = (int) args[0];
            if (cellIdx < 0 || cellIdx >= DACKit::kNumCells)
            { completion (makeStatusVar ("cell index out of range")); return; }
            processor.getDacKit().clearCell (cellIdx);
            completion (makeStatusVar ({}));
        });

    options = options.withNativeFunction ("getDacKit",
        [this] (const juce::Array<juce::var>&, Completion completion)
        {
            completion (buildDacKitVar (processor.getDacKit()));
        });

   #if ! GENVST_DEV_SERVER
    options = options.withResourceProvider ([] (const auto& url) { return getWebResource (url); });
   #endif

    return options;
}

void GenVstAudioProcessorEditor::emitNotify (const juce::String& level,
                                             const juce::String& message)
{
    auto* obj = new juce::DynamicObject();
    obj->setProperty ("level",   level);
    obj->setProperty ("message", message);
    webView.emitEventIfBrowserIsVisible ("notify", juce::var (obj));
}

void GenVstAudioProcessorEditor::runVgmExtractAsync (
    const juce::String& filePath,
    std::function<void (int, const juce::String&)> done)
{
    // SafePointer keeps the callback safe across editor destruction: if the
    // user closes the plugin window mid-extract, the message-thread bounce
    // becomes a no-op rather than dereferencing a destroyed editor.
    juce::Component::SafePointer<GenVstAudioProcessorEditor> safeThis (this);

    // juce::Thread::launch is fire-and-forget; the thread self-cleans when its
    // function returns. We do not need a member juce::Thread because the
    // extraction has no cancellation hooks and the message-thread bounce
    // handles the "editor already gone" case.
    juce::Thread::launch (
        [safeThis, filePath, done = std::move (done)]() mutable
        {
            // ---- Background thread: heavy parse ---------------------------
            const std::filesystem::path path { filePath.toRawUTF8() };
            std::string                  error;
            std::vector<Patch>           patches = extractFmPatches (path, error);

            // ---- Message thread: write + refresh + completion -------------
            juce::MessageManager::callAsync (
                [safeThis,
                 patches = std::move (patches),
                 error   = std::move (error),
                 done    = std::move (done)]() mutable
                {
                    if (safeThis == nullptr)
                        return;

                    if (patches.empty())
                    {
                        done (0, juce::String (error));
                        return;
                    }

                    auto& browser = safeThis->processor.getPatchBrowser();
                    auto  sr      = browser.saveExtractedPatches (patches);

                    if (sr.saved > 0)
                    {
                        // Tree + index refresh; matches the post-import path.
                        browser.rescanWritableRoots();
                        safeThis->emitPatchRootsChanged();
                    }

                    juce::String errMsg;
                    if (! sr.errors.empty())
                    {
                        juce::StringArray es;
                        for (const auto& e : sr.errors)
                            es.add (juce::String (e));
                        errMsg = es.joinIntoString ("; ");
                    }

                    done (sr.saved, errMsg);
                });
        });
}

void GenVstAudioProcessorEditor::emitPatchRootsChanged()
{
    // Pushed after any root-mutating action (save / import / delete / drop /
    // add folder). The patch-browser modal and the main-window quick-access
    // lists listen for this and re-call getRoots / getPatchList. Carrying an
    // empty payload is fine — the listeners always refetch the full snapshot.
    webView.emitEventIfBrowserIsVisible ("patchRootsChanged", juce::var());
}

bool GenVstAudioProcessorEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    // 05-ui-ux.md "File drag-and-drop": accept directories (registered as
    // custom roots) and any patch file whose extension is in
    // kSupportedPatchExtensions (imported). `.vgm`/`.vgz` also accepted —
    // they run through the VGM bank-import path (Task 21). Mixed drops are
    // fine — each item is dispatched in filesDropped.
    for (const auto& f : files)
    {
        const juce::File file (f);
        if (file.isDirectory())
            return true;
        const auto ext = file.getFileExtension().toLowerCase();
        if (ext == ".vgm" || ext == ".vgz")
            return true;
        if (isSupportedPatchExtension (ext.toStdString()))
            return true;
    }
    return false;
}

void GenVstAudioProcessorEditor::filesDropped (const juce::StringArray& files,
                                               int /*x*/, int /*y*/)
{
    auto& browser = processor.getPatchBrowser();
    bool changed = false;
    int  imported = 0;     // patch files copied (from both file + folder drops)
    juce::StringArray errors;

    for (const auto& f : files)
    {
        const juce::File file (f);
        if (file.isDirectory())
        {
            // Folder drop: recursively copy every supported patch file inside
            // the folder into the user-imported root so the patches appear in
            // the IMPORT tab. This was previously addCustomRoot — registering
            // a browser-only custom root made dropped patches invisible to
            // the main UI's pinned lists. Users explicitly wanting a custom
            // root use the Patch Browser's "Add Folder..." button instead.
            const auto r = browser.importPatchFolder (file.getFullPathName());
            imported += r.imported;
            for (const auto& e : r.errors) errors.add (juce::String (e));
            if (r.imported > 0) changed = true;
        }
        else
        {
            const auto ext = file.getFileExtension().toLowerCase();
            if (ext == ".vgm" || ext == ".vgz")
            {
                // VGM bank-import branch (Task 21). Same extraction path the
                // Import Bank button takes; the only difference is the
                // completion message goes through a toast rather than a JS
                // Promise. The async helper handles the root-refresh.
                const juce::String fileName = file.getFileName();
                runVgmExtractAsync (file.getFullPathName(),
                    [this, fileName] (int saved, const juce::String& err)
                    {
                        if (saved > 0)
                            emitNotify ("info",
                                "Imported " + juce::String (saved)
                                    + (saved == 1 ? " patch from " : " patches from ")
                                    + fileName);
                        if (err.isNotEmpty())
                            emitNotify ("error", err);
                        else if (saved == 0)
                            emitNotify ("error", "No patches imported from " + fileName);
                    });
                continue;
            }
            if (! isSupportedPatchExtension (ext.toStdString()))
                continue;   // silently skip non-patch files in a mixed drop
            const auto err = browser.importPatchFile (file.getFullPathName());
            if (! err.empty())
                errors.add (juce::String (err));
            else { ++imported; changed = true; }
        }
    }

    if (changed)
        emitPatchRootsChanged();

    if (! errors.isEmpty())
        emitNotify ("error", errors.joinIntoString ("; "));
    else if (imported > 0)
        emitNotify ("info",
            juce::String ("Imported ") + juce::String (imported)
                + " patch" + (imported == 1 ? "" : "es"));
}

GenVstAudioProcessorEditor::GenVstAudioProcessorEditor (GenVstAudioProcessor& proc)
    : juce::AudioProcessorEditor (proc),
      processor (proc),
      webView (makeOptions()),
      masterGainAttachment (*proc.getValueTreeState().getParameter ("master_gain"),
                            masterGainRelay,
                            proc.getValueTreeState().undoManager),
      psgMixAttachment       (*proc.getValueTreeState().getParameter ("psg_mix"),
                              psgMixRelay, proc.getValueTreeState().undoManager),
      psgLayerAttachment     (*proc.getValueTreeState().getParameter ("psg_layer"),
                              psgLayerRelay, proc.getValueTreeState().undoManager),
      psgNoiseTypeAttachment (*proc.getValueTreeState().getParameter ("psg_noise_type"),
                              psgNoiseTypeRelay, proc.getValueTreeState().undoManager),
      psgNoiseRateAttachment (*proc.getValueTreeState().getParameter ("psg_noise_rate"),
                              psgNoiseRateRelay, proc.getValueTreeState().undoManager),
      psgNoiseAutoAttachment (*proc.getValueTreeState().getParameter ("psg_noise_auto"),
                              psgNoiseAutoRelay, proc.getValueTreeState().undoManager),
      dacEnableAttachment    (*proc.getValueTreeState().getParameter ("dac_enable"),
                              dacEnableRelay, proc.getValueTreeState().undoManager),
      dacRateAttachment      (*proc.getValueTreeState().getParameter ("dac_rate"),
                              dacRateRelay, proc.getValueTreeState().undoManager),
      dacModeAttachment      (*proc.getValueTreeState().getParameter ("dac_mode"),
                              dacModeRelay, proc.getValueTreeState().undoManager),
      dacLevelAttachment     (*proc.getValueTreeState().getParameter ("dac_level"),
                              dacLevelRelay, proc.getValueTreeState().undoManager),
      bendRangeAttachment        (*proc.getValueTreeState().getParameter ("bend_range"),
                                  bendRangeRelay, proc.getValueTreeState().undoManager),
      velToTlAttachment          (*proc.getValueTreeState().getParameter ("vel_to_tl"),
                                  velToTlRelay, proc.getValueTreeState().undoManager),
      trueStereoAttachment       (*proc.getValueTreeState().getParameter ("true_stereo"),
                                  trueStereoRelay, proc.getValueTreeState().undoManager),
      aftertouchTargetAttachment (*proc.getValueTreeState().getParameter ("aftertouch_target"),
                                  aftertouchTargetRelay, proc.getValueTreeState().undoManager),
      voiceCountAttachment       (*proc.getValueTreeState().getParameter ("voice_count"),
                                  voiceCountRelay, proc.getValueTreeState().undoManager),
      uiScaleAttachment          (*proc.getValueTreeState().getParameter ("ui_scale"),
                                  uiScaleRelay, proc.getValueTreeState().undoManager),
      tooltipsEnabledAttachment  (*proc.getValueTreeState().getParameter ("tooltips_enabled"),
                                  tooltipsEnabledRelay, proc.getValueTreeState().undoManager)
     #if GENVST_DEV_SERVER
      , galleryKnobAttachment    (*proc.getValueTreeState().getParameter ("gallery_knob"),
                                  galleryKnobRelay,
                                  proc.getValueTreeState().undoManager)
      , gallerySliderAttachment  (*proc.getValueTreeState().getParameter ("gallery_slider"),
                                  gallerySliderRelay,
                                  proc.getValueTreeState().undoManager)
      , galleryReadoutAttachment (*proc.getValueTreeState().getParameter ("gallery_readout"),
                                  galleryReadoutRelay,
                                  proc.getValueTreeState().undoManager)
      , galleryStepAttachment    (*proc.getValueTreeState().getParameter ("gallery_step"),
                                  galleryStepRelay,
                                  proc.getValueTreeState().undoManager)
      , galleryToggleAttachment  (*proc.getValueTreeState().getParameter ("gallery_toggle"),
                                  galleryToggleRelay,
                                  proc.getValueTreeState().undoManager)
      , gallerySectionAttachment (*proc.getValueTreeState().getParameter ("gallery_section"),
                                  gallerySectionRelay,
                                  proc.getValueTreeState().undoManager)
      , galleryTabsAttachment    (*proc.getValueTreeState().getParameter ("gallery_tabs"),
                                  galleryTabsRelay,
                                  proc.getValueTreeState().undoManager)
      , galleryListAttachment    (*proc.getValueTreeState().getParameter ("gallery_list"),
                                  galleryListRelay,
                                  proc.getValueTreeState().undoManager)
     #endif
{
    setOpaque (true);
    addAndMakeVisible (webView);

    // Initial FM-part binding: pick up the persisted UI state so a project
    // reload returns to the user's last-edited part. State defaults to part 0
    // on a fresh instance; setStateInformation populates uiSelectedPart()
    // when restoring a saved project. selectChannel rebuilds these on every
    // subsequent part switch.
    opAttachments.resize ((std::size_t) (kNumOps * kNumOpParams));
    partAttachments.resize ((std::size_t) kNumPartParams);
    selectedPart = juce::jlimit (0, PartManager::kNumParts - 1,
                                 processor.uiSelectedPart());
    rebuildFmAttachments (selectedPart);

    // Per-PSG-channel attachments — one psg_vol_*, psg_pan_*, psg_bend_*
    // per channel. Heap-pinned to match the relays' NON_MOVEABLE storage.
    for (int i = 0; i < SN76489Engine::kNumChannels; ++i)
    {
        const juce::String suffix = (i == 0) ? "ch1"
                                  : (i == 1) ? "ch2"
                                  : (i == 2) ? "ch3"
                                             : "noise";
        auto& apvts = proc.getValueTreeState();

        psgVolAttachments[(std::size_t) i] = std::make_unique<juce::WebSliderParameterAttachment> (
            *apvts.getParameter ("psg_vol_"  + suffix), *psgVolRelays[(std::size_t) i],  apvts.undoManager);
        psgPanAttachments[(std::size_t) i] = std::make_unique<juce::WebSliderParameterAttachment> (
            *apvts.getParameter ("psg_pan_"  + suffix), *psgPanRelays[(std::size_t) i],  apvts.undoManager);
        psgBendAttachments[(std::size_t) i] = std::make_unique<juce::WebToggleButtonParameterAttachment> (
            *apvts.getParameter ("psg_bend_" + suffix), *psgBendRelays[(std::size_t) i], apvts.undoManager);

        // Task 23 — envelope attachments. Walk the 9 slider-param bases in
        // lockstep with the relay factory's order; the toggle (VEL) is its
        // own field.
        for (int p = 0; p < kNumPsgEnvSliderParams; ++p)
        {
            const auto paramId = juce::String (kPsgEnvSliderBases[(std::size_t) p])
                                  + "_" + suffix;
            if (auto* param = apvts.getParameter (paramId))
                psgEnvSliderAttachments[(std::size_t) p][(std::size_t) i] =
                    std::make_unique<juce::WebSliderParameterAttachment> (
                        *param,
                        *psgEnvSliderRelays[(std::size_t) p][(std::size_t) i],
                        apvts.undoManager);
        }
        if (auto* velParam = apvts.getParameter ("psg_vel_" + suffix))
            psgEnvVelAttachments[(std::size_t) i] =
                std::make_unique<juce::WebToggleButtonParameterAttachment> (
                    *velParam,
                    *psgEnvVelRelays[(std::size_t) i],
                    apvts.undoManager);
    }

    // Task 22 — Rack-routing attachments. Iterate the same suffix list +
    // param-base list used by makeRackRoutingRelays so the (slot, param)
    // ↔ relay-array index mapping stays in lockstep.
    {
        auto& apvts = proc.getValueTreeState();
        const auto suffixes = rackRoutingSuffixes();
        std::size_t idx = 0;
        for (std::size_t s = 0; s < (std::size_t) kNumRackSlotSuffixes; ++s)
            for (std::size_t p = 0; p < (std::size_t) kNumRackParamsPerSlot; ++p)
            {
                const auto paramId = juce::String (kRackRoutingParamBases[p])
                                   + suffixes[s];
                if (auto* param = apvts.getParameter (paramId))
                    rackRoutingAttachments[idx] =
                        std::make_unique<juce::WebSliderParameterAttachment> (
                            *param, *rackRoutingRelays[idx], apvts.undoManager);
                ++idx;
            }
    }

   #if GENVST_DEV_SERVER
    // Hot-reload workflow: load the Vite dev server (npm run dev in ui/)
    // instead of the embedded bundle. Vite is pinned to port 5173. The page
    // to load can be overridden via the GENVST_DEV_PAGE env variable so the
    // widget gallery (Task 10) can be opened inside the plugin window — e.g.
    // `set GENVST_DEV_PAGE=gallery.html` to launch the gallery against the
    // live apvts relays. Defaults to the main UI (`index.html`).
    const auto devPage = juce::SystemStats::getEnvironmentVariable (
                             "GENVST_DEV_PAGE", "");
    const auto devUrl  = devPage.isEmpty()
                             ? juce::String ("http://localhost:5173/")
                             : juce::String ("http://localhost:5173/") + devPage;
    webView.goToURL (devUrl);
   #else
    webView.goToURL (juce::WebBrowserComponent::getResourceProviderRoot());
   #endif

    setSize (960, 640);   // fixed window — ADR-0007

    // Cross-instance refresh: if another plugin instance imported / saved
    // patches into the shared user-roots while this instance's editor was
    // closed, our PatchBrowser still holds the stale tree. Re-scan once here
    // so the lists are correct from the first paint. The mtime-poll Timer
    // tracks subsequent changes while the editor is open.
    {
        auto& browser = processor.getPatchBrowser();
        browser.rescanWritableRoots();
        lastSavedMtime    = browser.userSavedRootMtime();
        lastImportedMtime = browser.userImportedRootMtime();
    }

    // ~30 Hz telemetry pump (08-ui-views.md "Header meter bay"). The interval
    // is 33 ms — close enough to 30 Hz; the audio thread runs an independent
    // VU release envelope so an occasional missed tick doesn't visibly freeze
    // the meter. Doubles as the cadence for the cross-instance mtime poll,
    // which runs every ~60 ticks (= ~2 s) via mtimePollTickCounter.
    startTimerHz (30);
}

GenVstAudioProcessorEditor::~GenVstAudioProcessorEditor()
{
    // Stop the telemetry pump before any of its dependencies vanish. The
    // base-class destructor would do this on its own, but doing it first
    // makes the "no callbacks during teardown" contract obvious.
    stopTimer();

    // Attachments must be destroyed before their relays. The vector cleanup is
    // explicit so the order is obvious; the unique_ptrs would do this on their
    // own destruction in the field-destructor order, but writing it out makes
    // the lifetime contract auditable.
    opAttachments.clear();
    partAttachments.clear();
    polyModeAttachment.reset();
    monoGlideAttachment.reset();
    unisonSpreadAttachment.reset();
    for (auto& a : psgVolAttachments)  a.reset();
    for (auto& a : psgPanAttachments)  a.reset();
    for (auto& a : psgBendAttachments) a.reset();
    for (auto& row : psgEnvSliderAttachments)
        for (auto& a : row) a.reset();
    for (auto& a : psgEnvVelAttachments) a.reset();
    for (auto& a : rackRoutingAttachments) a.reset();
}

void GenVstAudioProcessorEditor::rebuildFmAttachments (int part)
{
    auto& apvts        = processor.getValueTreeState();
    auto* undoManager  = apvts.undoManager;

    // Tear down old attachments first — each one holds a parameter listener
    // registered with the apvts, so destroying-before-constructing is the
    // correct order (avoids two attachments fighting over the same relay).
    for (auto& a : opAttachments)   a.reset();
    for (auto& a : partAttachments) a.reset();
    polyModeAttachment.reset();
    monoGlideAttachment.reset();
    unisonSpreadAttachment.reset();

    for (int op = 0; op < kNumOps; ++op)
    {
        for (int p = 0; p < kNumOpParams; ++p)
        {
            const auto id = opParamIdForPart (kFmOpParamIds[(std::size_t) p], op, part);
            auto* param = apvts.getParameter (id);
            jassert (param != nullptr);
            auto& relay = *opRelays[(std::size_t) (op * kNumOpParams + p)];
            opAttachments[(std::size_t) (op * kNumOpParams + p)] =
                std::make_unique<juce::WebSliderParameterAttachment> (
                    *param, relay, undoManager);
        }
    }

    for (int p = 0; p < kNumPartParams; ++p)
    {
        const auto id = partParamIdForPart (kFmPartParamIds[(std::size_t) p], part);
        auto* param = apvts.getParameter (id);
        jassert (param != nullptr);
        auto& relay = *partRelays[(std::size_t) p];
        partAttachments[(std::size_t) p] =
            std::make_unique<juce::WebSliderParameterAttachment> (
                *param, relay, undoManager);
    }

    // View 10 polyphony controls — same paging contract as the FM-part
    // relays. The IDs follow the `<base>_part<n>` convention used by the
    // PluginProcessor parameter layout.
    const juce::String suffix = "_part" + juce::String (part + 1);
    polyModeAttachment = std::make_unique<juce::WebComboBoxParameterAttachment> (
        *apvts.getParameter ("poly_mode" + suffix),
        polyModeRelay, undoManager);
    monoGlideAttachment = std::make_unique<juce::WebComboBoxParameterAttachment> (
        *apvts.getParameter ("mono_glide" + suffix),
        monoGlideRelay, undoManager);
    unisonSpreadAttachment = std::make_unique<juce::WebSliderParameterAttachment> (
        *apvts.getParameter ("unison_spread" + suffix),
        unisonSpreadRelay, undoManager);
}

void GenVstAudioProcessorEditor::paint (juce::Graphics& g)
{
    // The WebView covers the whole editor; this only shows for the instant
    // before the page first paints.
    g.fillAll (juce::Colours::black);
}

void GenVstAudioProcessorEditor::resized()
{
    webView.setBounds (getLocalBounds());
}

void GenVstAudioProcessorEditor::timerCallback()
{
    // Snapshot the audio-thread telemetry (atomics + lossy ring read), build
    // one combined event payload, push it to JS. emitEventIfBrowserIsVisible
    // is a no-op when the window is hidden, so the cost when the editor is
    // closed or occluded is just the snapshot read.
    auto& tel = processor.getTelemetry();

    const int n = tel.readScope (scopeScratch.data(), kScopeReadSamples);

    // Downsample by averaging contiguous chunks. If the ring hasn't filled
    // yet (cold start), we still emit a same-sized array — leading zeros
    // make the scope start flat-line and "fill in" rather than display a
    // jagged garbage trace.
    juce::Array<juce::var> scope;
    scope.ensureStorageAllocated (kScopeOutPoints);
    if (n > 0)
    {
        const double bucketSize = (double) n / (double) kScopeOutPoints;
        for (int i = 0; i < kScopeOutPoints; ++i)
        {
            const int lo = juce::jlimit (0, n,     (int) std::floor (i       * bucketSize));
            const int hi = juce::jlimit (0, n,     (int) std::floor ((i + 1) * bucketSize));
            if (hi <= lo)
            {
                scope.add (juce::var ((float) scopeScratch[(std::size_t) lo]));
                continue;
            }
            float sum = 0.0f;
            for (int s = lo; s < hi; ++s) sum += scopeScratch[(std::size_t) s];
            scope.add (juce::var (sum / (float) (hi - lo)));
        }
    }
    else
    {
        for (int i = 0; i < kScopeOutPoints; ++i)
            scope.add (juce::var (0.0f));
    }

    auto* payload = new juce::DynamicObject();
    payload->setProperty ("scope",     juce::var (scope));
    payload->setProperty ("vuL",       juce::var (tel.vuLeft()));
    payload->setProperty ("vuR",       juce::var (tel.vuRight()));
    payload->setProperty ("clip",      juce::var (tel.consumeClip()));
    payload->setProperty ("voiceMask", juce::var ((int) tel.voiceMask()));

    // Task 34 — slice the 10-bit rack-channel mask into per-row masks aligned
    // with the user's current rack ordering, so the JS widget can paint the
    // activity LEDs without re-fetching getRackState every tick. Each row's
    // mask carries only the bit for its assigned hardware channel.
    {
        const std::uint16_t rack = tel.rackChannelActivity();
        const auto& order = processor.getPartManager().getRackOrder();
        juce::Array<juce::var> rowMasks;
        rowMasks.ensureStorageAllocated ((int) order.size());
        for (const auto& slot : order)
        {
            std::uint16_t m = 0;
            switch (slot.type)
            {
                case PartManager::InstrumentType::FM:
                    if (slot.index >= 0 && slot.index < 6)
                        m = (std::uint16_t) (rack & (1u << slot.index));
                    break;
                case PartManager::InstrumentType::SQ:
                    if (slot.index >= 0 && slot.index < SN76489Engine::kNumToneChs)
                        m = (std::uint16_t) (rack & (1u << (6 + slot.index)));
                    else if (slot.index == SN76489Engine::kNoiseCh)
                        m = (std::uint16_t) (rack & (1u << 9));
                    break;
                case PartManager::InstrumentType::D:
                    // DAC plays back samples rather than keyed voices; the
                    // indicator semantics don't apply (task 34 out-of-scope).
                    m = 0;
                    break;
            }
            rowMasks.add (juce::var ((int) m));
        }
        payload->setProperty ("rowActiveMasks", juce::var (rowMasks));
    }

    webView.emitEventIfBrowserIsVisible ("meterData", juce::var (payload));

    // Task 16: drain any notifications setStateInformation queued before the
    // editor existed (or while the WebView wasn't visible) and surface each
    // as a toast. The queue lives on the processor so it survives editor
    // open/close cycles; this drain is idempotent when empty.
    processor.drainPendingNotifications ([this] (const auto& n)
    {
        emitNotify (n.level, n.message);
    });

    // Every ~2 s (60 timer ticks at 30 Hz), check whether another plugin
    // instance has imported / saved patches into the shared user roots and
    // refresh our PatchBrowser cache if so. See pollWritableRootsForExternalChanges().
    if (++mtimePollTickCounter >= 60)
    {
        mtimePollTickCounter = 0;
        pollWritableRootsForExternalChanges();
    }
}

void GenVstAudioProcessorEditor::pollWritableRootsForExternalChanges()
{
    auto& browser = processor.getPatchBrowser();
    const auto savedMtime    = browser.userSavedRootMtime();
    const auto importedMtime = browser.userImportedRootMtime();

    if (savedMtime == lastSavedMtime && importedMtime == lastImportedMtime)
        return;   // unchanged since last poll

    lastSavedMtime    = savedMtime;
    lastImportedMtime = importedMtime;

    if (browser.rescanWritableRoots())
        emitPatchRootsChanged();
}
