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
    // The embedded Vite bundle (genvst-ui.zip), opened once and shared by
    // every editor instance -- the binary data is static, read-only and
    // lives for the lifetime of the process.
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

    // The WebKit backends (macOS, Linux) reject @font-face files served with
    // a wrong or missing MIME type where Chromium is lenient -- every served
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

GenVstAudioProcessorEditor::GenVstAudioProcessorEditor (GenVstAudioProcessor& p)
    : juce::AudioProcessorEditor (&p),
      processor (p)
{
    // ADR-0023 -- fixed 1200x560 editor.
    setSize (1200, 560);
    setOpaque (true);

    // Fallback chrome -- only made visible when the WebView fails. Built
    // unconditionally so showFallbackPanel() can flip visibility without
    // having to allocate.
    fallbackTitle.setJustificationType (juce::Justification::centred);
    fallbackTitle.setFont (juce::FontOptions (22.0f));
    fallbackTitle.setColour (juce::Label::textColourId, juce::Colours::white);
    addChildComponent (fallbackTitle);

    fallbackMessage.setJustificationType (juce::Justification::centred);
    fallbackMessage.setFont (juce::FontOptions (14.0f));
    fallbackMessage.setColour (juce::Label::textColourId, juce::Colour (0xffd8dce0));
    fallbackMessage.setText (
        "The embedded WebView could not start. On Windows install the Microsoft\n"
        "Edge WebView2 Runtime, then click Retry. (08-ui-views.md view 9.)",
        juce::dontSendNotification);
    addChildComponent (fallbackMessage);

    retryButton.onClick = [this] { tryInitWebView(); };
    addChildComponent (retryButton);

    tryInitWebView();

    // ~30 Hz telemetry tick (05-ui-ux.md "C++ -> JS telemetry push"). Pushes
    // peakL/peakR/noteOn to the meterData event listeners. The webView pointer
    // can be null (fallback mode) -- emitEventIfBrowserIsVisible guards itself.
    startTimerHz (30);
}

GenVstAudioProcessorEditor::~GenVstAudioProcessorEditor()
{
    stopTimer();
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
            .withBackgroundColour (juce::Colour (0xff1c1f24)))
        .withNativeIntegrationEnabled()
        .withOptionsFrom (tooltipsEnabledRelay)
        .withOptionsFrom (galleryKnobA)
        .withOptionsFrom (galleryKnobB)
        .withOptionsFrom (galleryKnobC)
        .withOptionsFrom (galleryKnobD)
        .withOptionsFrom (galleryToggleA)
        .withOptionsFrom (galleryToggleB)
        .withOptionsFrom (galleryToggleC)
        .withOptionsFrom (galleryToggleD)
        .withOptionsFrom (galleryComboA)
        .withOptionsFrom (galleryAlgo)
        .withOptionsFrom (galleryStepper)
        .withOptionsFrom (galleryLevel)
        .withOptionsFrom (galleryNoteOn)
        .withOptionsFrom (galleryWheel)
        .withEventListener ("uiReady", [] (juce::var)
        {
            juce::Logger::writeToLog ("Gen VST: uiReady received from WebView");
        });

   #if ! GENVST_DEV_SERVER
    options = options.withResourceProvider (
        [] (const auto& url) { return getWebResource (url); });
   #endif

    return options;
}

void GenVstAudioProcessorEditor::tryInitWebView()
{
    // Tear down any previous attempt (Retry path).
    tooltipsAttachment.reset();
    galleryKnobAAtt.reset();
    galleryKnobBAtt.reset();
    galleryKnobCAtt.reset();
    galleryKnobDAtt.reset();
    galleryToggleAAtt.reset();
    galleryToggleBAtt.reset();
    galleryToggleCAtt.reset();
    galleryToggleDAtt.reset();
    galleryComboAAtt.reset();
    galleryAlgoAtt.reset();
    galleryStepperAtt.reset();
    galleryLevelAtt.reset();
    galleryNoteOnAtt.reset();
    galleryWheelAtt.reset();
    webView.reset();

    try
    {
        webView = std::make_unique<juce::WebBrowserComponent> (makeOptions());
    }
    catch (const std::exception& e)
    {
        juce::Logger::writeToLog (juce::String ("Gen VST: WebView init threw: ") + e.what());
        showFallbackPanel();
        return;
    }
    catch (...)
    {
        juce::Logger::writeToLog ("Gen VST: WebView init threw (unknown).");
        showFallbackPanel();
        return;
    }

    if (webView == nullptr)
    {
        showFallbackPanel();
        return;
    }

    auto& apvts = processor.getValueTreeState();

    auto makeSliderAtt = [&] (const juce::String& id, juce::WebSliderRelay& relay)
    {
        if (auto* param = apvts.getParameter (id))
            return std::make_unique<juce::WebSliderParameterAttachment> (
                *param, relay, apvts.undoManager);
        return std::unique_ptr<juce::WebSliderParameterAttachment> {};
    };
    auto makeToggleAtt = [&] (const juce::String& id, juce::WebToggleButtonRelay& relay)
    {
        if (auto* param = apvts.getParameter (id))
            return std::make_unique<juce::WebToggleButtonParameterAttachment> (
                *param, relay, apvts.undoManager);
        return std::unique_ptr<juce::WebToggleButtonParameterAttachment> {};
    };
    auto makeComboAtt = [&] (const juce::String& id, juce::WebComboBoxRelay& relay)
    {
        if (auto* param = apvts.getParameter (id))
            return std::make_unique<juce::WebComboBoxParameterAttachment> (
                *param, relay, apvts.undoManager);
        return std::unique_ptr<juce::WebComboBoxParameterAttachment> {};
    };

    tooltipsAttachment = makeToggleAtt ("tooltips_enabled", tooltipsEnabledRelay);
    galleryKnobAAtt    = makeSliderAtt ("gallery_knob_a",   galleryKnobA);
    galleryKnobBAtt    = makeSliderAtt ("gallery_knob_b",   galleryKnobB);
    galleryKnobCAtt    = makeSliderAtt ("gallery_knob_c",   galleryKnobC);
    galleryKnobDAtt    = makeSliderAtt ("gallery_knob_d",   galleryKnobD);
    galleryToggleAAtt  = makeToggleAtt ("gallery_toggle_a", galleryToggleA);
    galleryToggleBAtt  = makeToggleAtt ("gallery_toggle_b", galleryToggleB);
    galleryToggleCAtt  = makeToggleAtt ("gallery_toggle_c", galleryToggleC);
    galleryToggleDAtt  = makeToggleAtt ("gallery_toggle_d", galleryToggleD);
    galleryComboAAtt   = makeComboAtt  ("gallery_combo_a",  galleryComboA);
    galleryAlgoAtt     = makeSliderAtt ("gallery_algo",     galleryAlgo);
    galleryStepperAtt  = makeSliderAtt ("gallery_stepper",  galleryStepper);
    galleryLevelAtt    = makeSliderAtt ("gallery_level",    galleryLevel);
    galleryNoteOnAtt   = makeToggleAtt ("gallery_noteon",   galleryNoteOn);
    galleryWheelAtt    = makeSliderAtt ("gallery_wheel",    galleryWheel);

    addAndMakeVisible (*webView);
    webView->setBounds (getLocalBounds());

    fallbackTitle.setVisible (false);
    fallbackMessage.setVisible (false);
    retryButton.setVisible (false);

   #if GENVST_DEV_SERVER
    // Hot-reload workflow: load the Vite dev server (npm run dev in ui/)
    // instead of the embedded bundle. Vite is pinned to port 5173. The page
    // to load can be overridden via the GENVST_DEV_PAGE env variable so the
    // widget gallery can be opened inside the plugin window -- e.g.
    // `set GENVST_DEV_PAGE=gallery.html` to launch the gallery against the
    // live apvts relays. Defaults to the main UI (`index.html`).
    const auto devPage = juce::SystemStats::getEnvironmentVariable (
                             "GENVST_DEV_PAGE", "");
    const auto devUrl  = devPage.isEmpty()
                             ? juce::String ("http://localhost:5173/")
                             : juce::String ("http://localhost:5173/") + devPage;
    webView->goToURL (devUrl);
   #else
    webView->goToURL (juce::WebBrowserComponent::getResourceProviderRoot());
   #endif
}

void GenVstAudioProcessorEditor::showFallbackPanel()
{
    webView.reset();
    fallbackTitle.setVisible (true);
    fallbackMessage.setVisible (true);
    retryButton.setVisible (true);
    resized();   // re-layout the visible fallback chrome
    repaint();
}

void GenVstAudioProcessorEditor::paint (juce::Graphics& g)
{
    // Backdrop -- visible only behind the fallback panel; the WebView covers
    // the whole window when active.
    g.fillAll (juce::Colour (0xff111315));
}

void GenVstAudioProcessorEditor::resized()
{
    const auto b = getLocalBounds();

    if (webView != nullptr)
        webView->setBounds (b);

    // Fallback layout: centred title + message + Retry button.
    const int centreY = b.getCentreY();
    fallbackTitle.setBounds   (b.getX(), centreY - 80, b.getWidth(), 36);
    fallbackMessage.setBounds (b.getX(), centreY - 30, b.getWidth(), 48);

    const int btnW = 120;
    const int btnH = 32;
    retryButton.setBounds ((b.getWidth() - btnW) / 2,
                           centreY + 40,
                           btnW, btnH);
}

void GenVstAudioProcessorEditor::timerCallback()
{
    if (webView == nullptr)
        return;

    auto& t = processor.getTelemetry();
    auto* obj = new juce::DynamicObject();
    obj->setProperty ("peakL",  t.vuLeft());
    obj->setProperty ("peakR",  t.vuRight());
    obj->setProperty ("noteOn", t.noteOn());
    webView->emitEventIfBrowserIsVisible ("meterData", juce::var (obj));
}
