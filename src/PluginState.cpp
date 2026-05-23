#include "PluginState.h"

#include <filesystem>
#include <vector>

#include "DACPlayer.h"
#include "MidiRouter.h"
#include "PartManager.h"
#include "PatchBrowser.h"
#include "PluginProcessor.h"

namespace fs = std::filesystem;

namespace genvst::state
{

namespace
{
    constexpr const char* kPartsTag       = "parts";
    constexpr const char* kPartTag        = "part";
    constexpr const char* kPsgTag         = "psg";
    constexpr const char* kDacTag         = "dac";
    constexpr const char* kCustomRootsTag = "customRoots";
    constexpr const char* kRootTagInner   = "root";
    constexpr const char* kUiStateTag     = "uiState";
    // Task 22 — Rack active-slot tag. Holds which rack rows the user has
    // added (+) per type/index. The per-row routing values themselves live in
    // the apvts under midi_ch_* / transpose_* / note_lo/hi_* / detune_cents_*
    // / balance_*, so they save/restore through the existing apvts path.
    constexpr const char* kRackTag        = "rack";
    constexpr const char* kRackSlotTag    = "slot";

    using Kind = MidiRouter::Destination::Kind;

    int fmPartChannel (const MidiRouter& router, int part)
    {
        return router.destinationChannel (MidiRouter::destinationId ({ Kind::FmPart, part }));
    }

    int psgToneChannel (const MidiRouter& router, int tone)
    {
        return router.destinationChannel (MidiRouter::destinationId ({ Kind::PsgTone, tone }));
    }

    int psgNoiseChannel (const MidiRouter& router)
    {
        return router.destinationChannel (MidiRouter::destinationId ({ Kind::PsgNoise, 0 }));
    }

    int dacChannel (const MidiRouter& router)
    {
        return router.destinationChannel (MidiRouter::destinationId ({ Kind::Dac, 0 }));
    }

    fs::path toFsPath (const juce::String& s)
    {
       #if JUCE_WINDOWS
        return fs::path { s.toWideCharPointer() };
       #else
        return fs::path { s.toRawUTF8() };
       #endif
    }

    juce::String encodeBase64 (const std::uint8_t* data, std::size_t size)
    {
        if (data == nullptr || size == 0) return {};
        juce::MemoryOutputStream stream;
        juce::Base64::convertToBase64 (stream, data, size);
        return stream.toString();
    }

    std::vector<std::uint8_t> decodeBase64 (const juce::String& s)
    {
        juce::MemoryOutputStream stream;
        if (! juce::Base64::convertFromBase64 (stream, s))
            return {};
        const auto* bytes = static_cast<const std::uint8_t*> (stream.getData());
        return { bytes, bytes + stream.getDataSize() };
    }

    // Restore the routing destinations from the saved <parts>/<psg>/<dac> tags.
    // Missing fields fall back to whatever resetRouting set, so a partial state
    // (e.g. older save without a <psg> block) still leaves the routing usable.
    void restoreRouting (MidiRouter& router, const juce::XmlElement& root)
    {
        router.resetRouting();

        if (auto* partsEl = root.getChildByName (kPartsTag))
        {
            for (auto* partEl : partsEl->getChildWithTagNameIterator (kPartTag))
            {
                const int index = partEl->getIntAttribute ("index", -1);
                if (index < 0 || index >= PartManager::kNumParts) continue;
                const int ch = partEl->getIntAttribute ("midiChannel", -1);
                if (ch >= 0 && ch <= 16)
                    router.setDestinationChannel (
                        MidiRouter::destinationId ({ Kind::FmPart, index }), ch);
            }
        }

        if (auto* psgEl = root.getChildByName (kPsgTag))
        {
            const int ch0 = psgEl->getIntAttribute ("ch0",   -1);
            const int ch1 = psgEl->getIntAttribute ("ch1",   -1);
            const int ch2 = psgEl->getIntAttribute ("ch2",   -1);
            const int chN = psgEl->getIntAttribute ("noise", -1);
            if (ch0 >= 0) router.setDestinationChannel (MidiRouter::destinationId ({ Kind::PsgTone, 0 }), ch0);
            if (ch1 >= 0) router.setDestinationChannel (MidiRouter::destinationId ({ Kind::PsgTone, 1 }), ch1);
            if (ch2 >= 0) router.setDestinationChannel (MidiRouter::destinationId ({ Kind::PsgTone, 2 }), ch2);
            if (chN >= 0) router.setDestinationChannel (MidiRouter::destinationId ({ Kind::PsgNoise, 0 }), chN);
        }

        if (auto* dacEl = root.getChildByName (kDacTag))
        {
            const int ch = dacEl->getIntAttribute ("midiChannel", -1);
            if (ch >= 0)
                router.setDestinationChannel (MidiRouter::destinationId ({ Kind::Dac, 0 }), ch);
        }
    }

    // Restore DAC PCM (and the rate / display name) from the <dac> tag's pcm
    // attribute. Missing pcm = nothing to restore (the rate / enable / mode /
    // level scalars all live in the apvts and are restored elsewhere).
    void restoreDacPcm (DACPlayer& dac, const juce::XmlElement& root)
    {
        auto* dacEl = root.getChildByName (kDacTag);
        if (dacEl == nullptr) return;

        const auto pcmString = dacEl->getStringAttribute ("pcm");
        if (pcmString.isEmpty())
        {
            dac.clearPcm();
            return;
        }

        auto bytes = decodeBase64 (pcmString);
        if (bytes.empty())
        {
            dac.clearPcm();
            return;
        }

        // The rate stored alongside the PCM is the rate the bytes were
        // resampled to; the DAC must match so playback timing is correct. The
        // apvts dac_rate is restored separately by apvts.replaceState and
        // would re-trigger a resample on the next processBlock if they
        // disagreed — saving them together keeps them in lockstep.
        const int rate = dacEl->getIntAttribute ("rate", 22050);
        const auto name = dacEl->getStringAttribute ("name");
        dac.loadRawPcm (bytes.data(), bytes.size(), rate, name);
    }

    // Returns the list of custom-root paths from the <customRoots> tag.
    std::vector<juce::String> collectCustomRoots (const juce::XmlElement& root)
    {
        std::vector<juce::String> out;
        if (auto* el = root.getChildByName (kCustomRootsTag))
        {
            out.reserve ((std::size_t) el->getNumChildElements());
            for (auto* child : el->getChildWithTagNameIterator (kRootTagInner))
            {
                const auto path = child->getStringAttribute ("path");
                if (path.isNotEmpty())
                    out.push_back (path);
            }
        }
        return out;
    }

    // Build the per-part patchPath array from the <parts> tag.
    std::array<juce::String, PartManager::kNumParts>
    collectPatchPaths (const juce::XmlElement& root)
    {
        std::array<juce::String, PartManager::kNumParts> out;
        if (auto* partsEl = root.getChildByName (kPartsTag))
        {
            for (auto* partEl : partsEl->getChildWithTagNameIterator (kPartTag))
            {
                const int index = partEl->getIntAttribute ("index", -1);
                if (index < 0 || index >= PartManager::kNumParts) continue;
                out[(std::size_t) index] = partEl->getStringAttribute ("patchPath");
            }
        }
        return out;
    }

    // Reset every custom root the browser currently holds, leaving Factory /
    // UserSaved / UserImported intact. State-restore is a full replacement —
    // any manually-added roots from before the project load are discarded.
    void clearCustomRoots (genvst::PatchBrowser& browser)
    {
        std::vector<juce::String> ids;
        for (const auto& r : browser.roots())
            if (r->kind == genvst::PatchRootKind::Custom)
                ids.push_back (r->id);
        for (const auto& id : ids)
            browser.removeCustomRoot (id);
    }

    // Replay the deferred custom-root + per-part patch reloads onto a
    // now-initialised patch browser. Missing paths emit a notify toast and
    // are otherwise skipped — the part keeps its restored apvts values, and
    // the rest of the project still loads (16-state-persistence.md
    // "Verification" step 4).
    void applyPatchAndRootRestoreImpl (GenVstAudioProcessor& proc,
                                       const std::vector<juce::String>& roots,
                                       const std::array<juce::String, PartManager::kNumParts>& paths)
    {
        auto& browser = proc.getPatchBrowser();
        clearCustomRoots (browser);

        for (const auto& path : roots)
        {
            std::error_code ec;
            if (! fs::is_directory (toFsPath (path), ec))
            {
                proc.addPendingNotification ("warning",
                    "Patch folder could not be resolved: " + path);
                continue;
            }
            const auto id = browser.addCustomRoot (path);
            if (id.isEmpty())
                proc.addPendingNotification ("warning",
                    "Could not register patch folder: " + path);
        }

        for (int part = 0; part < PartManager::kNumParts; ++part)
        {
            const auto& path = paths[(std::size_t) part];
            if (path.isEmpty()) continue;

            std::error_code ec;
            if (! fs::is_regular_file (toFsPath (path), ec))
            {
                proc.addPendingNotification ("warning",
                    "Patch file could not be resolved: " + path);
                continue;
            }

            const auto err = browser.loadIntoPart (part, path);
            if (! err.empty())
                proc.addPendingNotification ("warning",
                    juce::String ("Could not reload patch: ") + juce::String (err));
        }
    }
}

std::unique_ptr<juce::XmlElement> save (GenVstAudioProcessor& proc)
{
    auto root = std::make_unique<juce::XmlElement> (kRootTag);
    root->setAttribute ("schemaVersion", 1);

    // The full apvts parameter tree as a nested child. apvts.copyState() is
    // the same call the legacy getStateInformation used; wrapping it in our
    // root XML lets setStateInformation distinguish new-format vs legacy
    // states without breaking projects saved before Task 16.
    if (auto apvtsXml = proc.getValueTreeState().copyState().createXml())
        root->addChildElement (apvtsXml.release());

    auto& router  = proc.getMidiRouter();
    auto& browser = proc.getPatchBrowser();
    auto& dac     = proc.getDacPlayer();

    auto* partsEl = root->createNewChildElement (kPartsTag);
    for (int part = 0; part < PartManager::kNumParts; ++part)
    {
        auto* partEl = partsEl->createNewChildElement (kPartTag);
        partEl->setAttribute ("index",       part);
        partEl->setAttribute ("midiChannel", fmPartChannel (router, part));
        partEl->setAttribute ("patchPath",   browser.activePatchPath (part));
    }

    auto* psgEl = root->createNewChildElement (kPsgTag);
    psgEl->setAttribute ("ch0",   psgToneChannel (router, 0));
    psgEl->setAttribute ("ch1",   psgToneChannel (router, 1));
    psgEl->setAttribute ("ch2",   psgToneChannel (router, 2));
    psgEl->setAttribute ("noise", psgNoiseChannel (router));

    auto* dacEl = root->createNewChildElement (kDacTag);
    dacEl->setAttribute ("midiChannel", dacChannel (router));
    dacEl->setAttribute ("rate",        dac.getDacRate());
    if (dac.getSampleName().isNotEmpty())
        dacEl->setAttribute ("name", dac.getSampleName());
    if (dac.hasPcm())
        dacEl->setAttribute ("pcm",
            encodeBase64 (dac.getRawPcmData(), dac.getRawPcmSize()));

    auto* rootsEl = root->createNewChildElement (kCustomRootsTag);
    for (const auto& r : browser.roots())
    {
        if (r->kind != genvst::PatchRootKind::Custom) continue;
        if (r->folder == nullptr || r->folder->path.isEmpty()) continue;
        auto* entry = rootsEl->createNewChildElement (kRootTagInner);
        entry->setAttribute ("path", r->folder->path);
    }

    // Editor UI selection state (which FM channel was last edited and which
    // preset/import tab was active). Restored on the next mount so reopening
    // a project picks up where the user left off.
    auto* uiEl = root->createNewChildElement (kUiStateTag);
    uiEl->setAttribute ("selectedPart", proc.uiSelectedPart());
    uiEl->setAttribute ("presetTab",    proc.uiPresetTab());

    // Task 22 — Rack active-slot snapshot. Iterate every rack slot type and
    // emit a <slot type="fm" index="0"/> entry for each currently-active slot.
    // The per-slot routing values themselves ride on the apvts.
    auto& parts = proc.getPartManager();
    auto* rackEl = root->createNewChildElement (kRackTag);
    auto saveActiveSlots = [&] (PartManager::InstrumentType type, const char* label)
    {
        const int n = PartManager::slotPoolSize (type);
        for (int i = 0; i < n; ++i)
        {
            const PartManager::SlotId slot { type, i };
            if (! parts.isSlotActive (slot)) continue;
            auto* el = rackEl->createNewChildElement (kRackSlotTag);
            el->setAttribute ("type",  label);
            el->setAttribute ("index", i);
        }
    };
    saveActiveSlots (PartManager::InstrumentType::FM, "fm");
    saveActiveSlots (PartManager::InstrumentType::SQ, "sq");
    saveActiveSlots (PartManager::InstrumentType::D,  "d");

    return root;
}

void restore (GenVstAudioProcessor& proc, const juce::XmlElement& xml)
{
    // Two accepted formats:
    //   * Pre-Task-16: bare apvts element (tag == apvts.state.getType()).
    //     Restore apvts only; leave routing / roots / DAC at defaults.
    //   * Task-16+:    <GenVstState ...> wrapper containing the apvts as
    //     its first child plus the custom non-apvts fields.
    auto& apvts = proc.getValueTreeState();
    const auto apvtsTag = apvts.state.getType().toString();

    const juce::XmlElement* wrapper = nullptr;
    const juce::XmlElement* apvtsEl = nullptr;

    if (xml.hasTagName (apvtsTag))
    {
        apvtsEl = &xml;
    }
    else if (xml.hasTagName (kRootTag))
    {
        wrapper = &xml;
        apvtsEl = xml.getChildByName (apvtsTag);
    }
    else
    {
        // Unknown format — leave the plugin at its constructor defaults. No
        // toast: this typically only happens if a host hands us garbage state.
        return;
    }

    if (apvtsEl != nullptr)
        apvts.replaceState (juce::ValueTree::fromXml (*apvtsEl));

    if (wrapper == nullptr)
        return;   // legacy state — nothing more to restore

    restoreRouting (proc.getMidiRouter(), *wrapper);
    restoreDacPcm  (proc.getDacPlayer(),  *wrapper);

    // Task 22 — Restore rack active-slot state. Default-reset every slot,
    // then re-enable those listed in <rack>. Missing tag = no rack history;
    // legacy projects then surface with whichever slots had something loaded
    // (FM slot 0 is the constructor default — i.e. one row visible).
    {
        auto& parts = proc.getPartManager();
        for (int i = 0; i < PartManager::kNumRackFmSlots; ++i)
            parts.setSlotActive ({ PartManager::InstrumentType::FM, i }, false);
        for (int i = 0; i < PartManager::kNumRackSqSlots; ++i)
            parts.setSlotActive ({ PartManager::InstrumentType::SQ, i }, false);
        for (int i = 0; i < PartManager::kNumRackDSlots; ++i)
            parts.setSlotActive ({ PartManager::InstrumentType::D,  i }, false);

        if (auto* rackEl = wrapper->getChildByName (kRackTag))
        {
            for (auto* slotEl : rackEl->getChildWithTagNameIterator (kRackSlotTag))
            {
                const auto typeStr = slotEl->getStringAttribute ("type");
                const int  idx     = slotEl->getIntAttribute ("index", -1);
                if (idx < 0) continue;
                PartManager::InstrumentType type;
                if      (typeStr == "fm") type = PartManager::InstrumentType::FM;
                else if (typeStr == "sq") type = PartManager::InstrumentType::SQ;
                else if (typeStr == "d")  type = PartManager::InstrumentType::D;
                else continue;
                if (idx >= PartManager::slotPoolSize (type)) continue;
                parts.setSlotActive ({ type, idx }, true);
            }
        }
        else
        {
            // Legacy state — re-mark FM slot 0 so the UI shows the default row.
            parts.setSlotActive ({ PartManager::InstrumentType::FM, 0 }, true);
        }
    }

    // Restore the editor UI selection state from <uiState ...>. Missing or
    // out-of-range values fall back to the constructor defaults (part 0,
    // PRESETS tab) so legacy Task-16 projects without this element still load.
    if (auto* uiEl = wrapper->getChildByName (kUiStateTag))
    {
        proc.setUiSelectedPart (uiEl->getIntAttribute ("selectedPart", 0));
        proc.setUiPresetTab    (uiEl->getIntAttribute ("presetTab",    0));
    }

    auto customRoots = collectCustomRoots (*wrapper);
    auto patchPaths  = collectPatchPaths  (*wrapper);

    // The patch browser may not be initialised yet (some DAWs call
    // setStateInformation before the first prepareToPlay). Apply
    // immediately if we can; otherwise stash for prepareToPlay to replay.
    auto& pending = proc.pendingStateRestoreData();
    pending.customRootPaths = std::move (customRoots);
    pending.patchPaths      = std::move (patchPaths);
    pending.active          = true;

    applyPendingPatchAndRootRestore (proc);
}

void applyPendingPatchAndRootRestore (GenVstAudioProcessor& proc)
{
    auto& pending = proc.pendingStateRestoreData();
    if (! pending.active) return;

    // The browser is "ready enough" as soon as roots() is non-empty (i.e.
    // initialize ran). If it's still empty we leave pending active and the
    // first prepareToPlay will retry.
    auto& browser = proc.getPatchBrowser();
    if (browser.roots().empty())
        return;

    applyPatchAndRootRestoreImpl (proc, pending.customRootPaths, pending.patchPaths);

    pending.customRootPaths.clear();
    pending.patchPaths.fill ({});
    pending.active = false;
}

} // namespace genvst::state
