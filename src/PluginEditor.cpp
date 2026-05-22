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
        .withEventListener ("uiReady", [] (juce::var)
        {
            juce::Logger::writeToLog ("Gen VST: uiReady received from WebView");
        });

   #if ! GENVST_DEV_SERVER
    options = options.withResourceProvider ([] (const auto& url) { return getWebResource (url); });
   #endif

    return options;
}

GenVstAudioProcessorEditor::GenVstAudioProcessorEditor (GenVstAudioProcessor& processor)
    : juce::AudioProcessorEditor (processor),
      webView (makeOptions()),
      masterGainAttachment (*processor.getValueTreeState().getParameter ("master_gain"),
                            masterGainRelay,
                            processor.getValueTreeState().undoManager)
{
    setOpaque (true);
    addAndMakeVisible (webView);

   #if GENVST_DEV_SERVER
    // Hot-reload workflow: load the Vite dev server (npm run dev in ui/)
    // instead of the embedded bundle. Vite is pinned to port 5173.
    webView.goToURL ("http://localhost:5173/");
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
