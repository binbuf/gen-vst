#include "PluginEditor.h"

#include <cmath>

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

    // Build the JS-side DAC info object consumed by the D-section view
    // (08-ui-views.md view 3) — name, length, bit-depth and a peaks array.
    // The `empty` flag is set when no sample is loaded so the JS can render
    // the empty-state placeholder.
    juce::var buildDacInfoVar (const DACPlayer& dac, int numPeakBuckets)
    {
        auto* obj = new juce::DynamicObject();
        if (! dac.hasPcm())
        {
            obj->setProperty ("empty", true);
            return juce::var (obj);
        }
        obj->setProperty ("name",      dac.getSampleName());
        obj->setProperty ("lengthSec", dac.getSampleLengthSeconds());
        obj->setProperty ("bitDepth",  dac.getSampleBitDepth());

        juce::Array<juce::var> peaks;
        for (float p : dac.computePeaks (numPeakBuckets))
            peaks.add (juce::var (p));
        obj->setProperty ("peaks", juce::var (peaks));
        return juce::var (obj);
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

            // Routing + DAC sample + active patch paths.
            processor.getMidiRouter().resetRouting();
            processor.getDacPlayer().clearPcm();
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

    // --- DAC (Task 13 D view) ------------------------------------------------
    // LOAD WAV… uses the native juce::FileChooser (08-ui-views.md view 11)
    // and feeds the result into DACPlayer::loadWav. Failure surfaces through
    // emitNotify, then resolves to {ok:false}. The chooser is launched
    // asynchronously, so completion is captured by the lambda.
    options = options.withNativeFunction ("loadWavDialog",
        [this] (const juce::Array<juce::var>&, Completion completion)
        {
            wavChooser = std::make_unique<juce::FileChooser> (
                "Load WAV", juce::File{}, "*.wav");

            const auto flags = juce::FileBrowserComponent::openMode
                             | juce::FileBrowserComponent::canSelectFiles;

            wavChooser->launchAsync (flags,
                [this, completion = std::move (completion)] (const juce::FileChooser& fc) mutable
                {
                    const juce::File file = fc.getResult();
                    if (file == juce::File{})
                    {
                        // User cancelled — silent.
                        completion (makeStatusVar ({}));
                        return;
                    }
                    if (! processor.getDacPlayer().loadWav (file))
                    {
                        emitNotify ("error", "Failed to load WAV: " + file.getFileName());
                        completion (makeStatusVar ("WAV load failed"));
                        return;
                    }

                    auto* obj = new juce::DynamicObject();
                    obj->setProperty ("ok",   true);
                    obj->setProperty ("info", buildDacInfoVar (processor.getDacPlayer(), 220));
                    completion (juce::var (obj));
                });
        });

    options = options.withNativeFunction ("clearDac",
        [this] (const juce::Array<juce::var>&, Completion completion)
        {
            processor.getDacPlayer().clearPcm();
            completion (makeStatusVar ({}));
        });

    options = options.withNativeFunction ("getDacInfo",
        [this] (const juce::Array<juce::var>& args, Completion completion)
        {
            int numBuckets = 220;
            if (! args.isEmpty() && args[0].isInt())
                numBuckets = juce::jlimit (16, 1024, (int) args[0]);
            completion (buildDacInfoVar (processor.getDacPlayer(), numBuckets));
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
    // kSupportedPatchExtensions (imported). Mixed drops are fine — each item
    // is dispatched in filesDropped.
    for (const auto& f : files)
    {
        const juce::File file (f);
        if (file.isDirectory())
            return true;
        const auto ext = file.getFileExtension().toLowerCase().toStdString();
        if (isSupportedPatchExtension (ext))
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
            const auto ext = file.getFileExtension().toLowerCase().toStdString();
            if (! isSupportedPatchExtension (ext))
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
