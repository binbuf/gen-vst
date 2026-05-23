#include "PluginEditor.h"

#if ! GENVST_DEV_SERVER
 #include <memory>
 #include <optional>
 #include <unordered_map>
 #include <vector>

 #include "GenVstWebData.h"
#endif

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
            // Task 14 owns the attachment-rebind that paints the new part's
            // values into the UI relays. For now we just track the index — the
            // patch-load native functions target it correctly.
            auto* obj = new juce::DynamicObject();
            obj->setProperty ("ok",   true);
            obj->setProperty ("part", n);
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
