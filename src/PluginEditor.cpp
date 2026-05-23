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
    options = options.withNativeFunction ("loadInstrument",
        [this] (const juce::Array<juce::var>& args, Completion completion)
        {
            if (args.isEmpty() || ! args[0].isString())
            { completion (makeStatusVar ("path required")); return; }
            auto err = processor.getPatchBrowser().loadIntoPart (selectedPart, args[0].toString());
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
            completion (makeStatusVar (err));
        });

    // --- Save / Import / Export (file-chooser bodies are Task 14) ----------
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

    // --- Channel paging ------------------------------------------------------
    options = options.withNativeFunction ("selectChannel",
        [this] (const juce::Array<juce::var>& args, Completion completion)
        {
            if (args.isEmpty() || ! args[0].isInt())
            { completion (makeStatusVar ("part index required")); return; }
            const int n = juce::jlimit (0, PartManager::kNumParts - 1, (int) args[0]);
            selectedPart = n;
            rebuildFmAttachments (n);
            auto* obj = new juce::DynamicObject();
            obj->setProperty ("ok",   true);
            obj->setProperty ("part", n);
            completion (juce::var (obj));
        });

    // --- Section switch (FM / SQ / D) ---------------------------------------
    // The FM region is built in this task; SQ and D land in Task 13. Until
    // then this function records the selection and reports it back so the JS
    // can render the placeholder regions, but it has no effect on the C++
    // side (no attachments to rebuild — the SQ/D relays are part of the
    // global set and bind once).
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

   #if ! GENVST_DEV_SERVER
    options = options.withResourceProvider ([] (const auto& url) { return getWebResource (url); });
   #endif

    return options;
}

GenVstAudioProcessorEditor::GenVstAudioProcessorEditor (GenVstAudioProcessor& proc)
    : juce::AudioProcessorEditor (proc),
      processor (proc),
      webView (makeOptions()),
      masterGainAttachment (*proc.getValueTreeState().getParameter ("master_gain"),
                            masterGainRelay,
                            proc.getValueTreeState().undoManager)
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

    // Initial FM-part binding: every FM relay attaches to part 0 (CHANNELS
    // button 1 in the layout). selectChannel rebuilds these on every part
    // switch.
    opAttachments.resize ((std::size_t) (kNumOps * kNumOpParams));
    partAttachments.resize ((std::size_t) kNumPartParams);
    rebuildFmAttachments (0);

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

    setSize (960, 560);   // fixed window — ADR-0007

    // ~30 Hz telemetry pump (08-ui-views.md "Header meter bay"). The interval
    // is 33 ms — close enough to 30 Hz; the audio thread runs an independent
    // VU release envelope so an occasional missed tick doesn't visibly freeze
    // the meter.
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
}
