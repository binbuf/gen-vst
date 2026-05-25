#include "PluginEditor.h"

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

#include "PatchSystem.h"
#include "PsgPreset.h"
#include "VgmExtract.h"

#if ! GENVST_DEV_SERVER
 #include <memory>
 #include <optional>
 #include <unordered_map>

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

    // Task 09 — patch-loaded callback. The processor invokes this on the
    // message thread after a preset has been applied; we relay it into the
    // WebView so the header LCD picks up the new name.
    processor.setPatchLoadedNotifier (
        [this] (const PatchLoadedNotifier& note) { emitPatchLoaded (note); });

    // Task 11 — state-restore toast notifier. Routes restore-time issues
    // (unresolved patch path, unresolved custom root, legacy v1 state) into
    // the WebView's notify channel. Registering here also drains any toasts
    // the processor queued before the editor existed.
    processor.setStateRestoreNotifier (
        [this] (const juce::String& level, const juce::String& message)
        { emitToast (level, message); });
}

GenVstAudioProcessorEditor::~GenVstAudioProcessorEditor()
{
    stopTimer();
    processor.setPatchLoadedNotifier ({});
    processor.setStateRestoreNotifier ({});
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
        // Task 08 — header + Settings relays.
        .withOptionsFrom (modeSelectRelay)
        .withOptionsFrom (outputFilterRelay)
        .withOptionsFrom (ladderEffectRelay)
        .withOptionsFrom (masterVolumeRelay)
        .withOptionsFrom (fmDacPrescalerRelay)
        .withOptionsFrom (prescalerRelay)
        .withOptionsFrom (hardwareStrictRelay)
        .withOptionsFrom (velocityToTlRelay)
        .withOptionsFrom (aftertouchTargetRelay)
        .withOptionsFrom (uiScaleRelay)
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
        .withEventListener ("uiReady", [] (juce::var payload)
        {
            // Carries the JS-side init() outcome: { ok: true } on success,
            // or { ok: false, error: "...", stack: "..." } when the
            // permanent try/catch in ui/src/main.js caught a mount throw.
            // Logging the payload here turns an otherwise-silent JS error
            // into a one-line entry in the host's juce::Logger output.
            juce::Logger::writeToLog ("Gen VST: uiReady received from WebView: "
                                      + juce::JSON::toString (payload));
        })
        // Task 08 — Settings → RESET ALL TO DEFAULTS confirmation handler.
        // Returns juce::var{} so the JS-side getNativeFunction promise resolves.
        .withNativeFunction ("resetAllToDefaults",
            [this] (const juce::Array<juce::var>& /*args*/,
                    juce::WebBrowserComponent::NativeFunctionCompletion completion)
            {
                processor.resetAllParametersToDefaults();
                completion (juce::var{});
            })
        // Task 09 — tagged preset browser native API.
        .withNativeFunction ("patchNav",
            [this] (const juce::Array<juce::var>& args,
                    juce::WebBrowserComponent::NativeFunctionCompletion completion)
            { doPatchNav (args, std::move (completion)); })
        .withNativeFunction ("loadPatch",
            [this] (const juce::Array<juce::var>& args,
                    juce::WebBrowserComponent::NativeFunctionCompletion completion)
            { doLoadPatch (args, std::move (completion)); })
        .withNativeFunction ("savePatch",
            [this] (const juce::Array<juce::var>& args,
                    juce::WebBrowserComponent::NativeFunctionCompletion completion)
            { doSavePatch (args, std::move (completion)); })
        .withNativeFunction ("importPatch",
            [this] (const juce::Array<juce::var>& args,
                    juce::WebBrowserComponent::NativeFunctionCompletion completion)
            { doImportPatch (args, std::move (completion)); })
        .withNativeFunction ("exportPatch",
            [this] (const juce::Array<juce::var>& args,
                    juce::WebBrowserComponent::NativeFunctionCompletion completion)
            { doExportPatch (args, std::move (completion)); })
        .withNativeFunction ("addPatchRoot",
            [this] (const juce::Array<juce::var>& args,
                    juce::WebBrowserComponent::NativeFunctionCompletion completion)
            { doAddPatchRoot (args, std::move (completion)); })
        .withNativeFunction ("deletePatch",
            [this] (const juce::Array<juce::var>& args,
                    juce::WebBrowserComponent::NativeFunctionCompletion completion)
            { doDeletePatch (args, std::move (completion)); })
        .withNativeFunction ("expandFolder",
            [this] (const juce::Array<juce::var>& args,
                    juce::WebBrowserComponent::NativeFunctionCompletion completion)
            { doExpandFolder (args, std::move (completion)); })
        .withNativeFunction ("getPatchList",
            [this] (const juce::Array<juce::var>& args,
                    juce::WebBrowserComponent::NativeFunctionCompletion completion)
            { doGetPatchList (args, std::move (completion)); })
        .withNativeFunction ("getPatchRoots",
            [this] (const juce::Array<juce::var>& args,
                    juce::WebBrowserComponent::NativeFunctionCompletion completion)
            { doGetPatchRoots (args, std::move (completion)); });

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
    modeSelectAtt.reset();
    outputFilterAtt.reset();
    ladderEffectAtt.reset();
    masterVolumeAtt.reset();
    fmDacPrescalerAtt.reset();
    prescalerAtt.reset();
    hardwareStrictAtt.reset();
    velocityToTlAtt.reset();
    aftertouchTargetAtt.reset();
    uiScaleAtt.reset();
    uiScaleListener.reset();
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

    // Task 08 — header + Settings attachments.
    modeSelectAtt       = makeComboAtt  ("mode_select",       modeSelectRelay);
    outputFilterAtt     = makeToggleAtt ("output_filter",     outputFilterRelay);
    ladderEffectAtt     = makeToggleAtt ("ladder_effect",     ladderEffectRelay);
    masterVolumeAtt     = makeSliderAtt ("master_volume",     masterVolumeRelay);
    fmDacPrescalerAtt   = makeSliderAtt ("fm_dac_prescaler",  fmDacPrescalerRelay);
    prescalerAtt        = makeSliderAtt ("prescaler",         prescalerRelay);
    hardwareStrictAtt   = makeToggleAtt ("hardware_strict",   hardwareStrictRelay);
    velocityToTlAtt     = makeToggleAtt ("velocity_to_tl",    velocityToTlRelay);
    aftertouchTargetAtt = makeComboAtt  ("aftertouch_target", aftertouchTargetRelay);
    uiScaleAtt          = makeComboAtt  ("ui_scale",          uiScaleRelay);

    // UI scale listener — pulls the choice index, applies an integer zoom
    // to the editor host bounds (ADR-0017 + ADR-0023). Hooked into the
    // apvts parameter via juce::ParameterAttachment so the listener is
    // disposed together with the WebView teardown.
    if (auto* uiScaleParam = apvts.getParameter ("ui_scale"))
    {
        uiScaleListener = std::make_unique<juce::ParameterAttachment> (
            *uiScaleParam,
            [this] (float normalised)
            {
                // 3-choice param: round to the nearest integer choice index
                // (0/1/2), then map to a 1×/2×/3× whole-window zoom.
                const int idx = juce::jlimit (0, 2, juce::roundToInt (normalised * 2.0f));
                applyUiScale (idx + 1);
            },
            apvts.undoManager);
        uiScaleListener->sendInitialUpdate();
    }

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

void GenVstAudioProcessorEditor::applyUiScale (int n)
{
    // ADR-0017 + ADR-0023 — integer presets only; resize the editor's
    // outer bounds so the host's window grows with the scale, then the
    // WebView naturally upscales the 1200x560 page bitmap. The page
    // itself does not need to know about the scale; the WebView's nearest-
    // neighbour upscale handles it cleanly.
    const int scale = juce::jlimit (1, 3, n);
    setSize (1200 * scale, 560 * scale);
    // resized() lays out the WebView (and the fallback chrome) against
    // getLocalBounds(), so it picks up the new size on the next call.
    if (webView != nullptr)
        webView->setBounds (getLocalBounds());
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

// =============================================================================
// Task 09 — preset browser native API + drag-and-drop
// =============================================================================

void GenVstAudioProcessorEditor::emitPatchLoaded (const PatchLoadedNotifier& note)
{
    if (webView == nullptr) return;
    auto* obj = new juce::DynamicObject();
    obj->setProperty ("name", note.name);
    obj->setProperty ("tag",  juce::String (note.tag == Tag::SQ ? "SQ" : "FM"));
    obj->setProperty ("path", note.path);
    webView->emitEventIfBrowserIsVisible ("patchLoaded", juce::var (obj));
}

void GenVstAudioProcessorEditor::emitToast (const juce::String& level,
                                            const juce::String& message)
{
    if (webView == nullptr) return;
    auto* obj = new juce::DynamicObject();
    obj->setProperty ("level",   level);
    obj->setProperty ("message", message);
    webView->emitEventIfBrowserIsVisible ("notify", juce::var (obj));
}

namespace
{
    juce::String stringArg (const juce::Array<juce::var>& args, int idx)
    {
        return idx < args.size() ? args[idx].toString() : juce::String{};
    }

    int intArg (const juce::Array<juce::var>& args, int idx, int fallback)
    {
        if (idx >= args.size()) return fallback;
        const auto& v = args.getReference (idx);
        if (v.isInt() || v.isInt64() || v.isDouble()) return static_cast<int> (v);
        if (v.isString())
        {
            const auto s = v.toString();
            if (s == "prev" || s == "-1") return -1;
            if (s == "next" || s == "+1" || s == "1") return 1;
            return s.getIntValue();
        }
        return fallback;
    }

    juce::var resultObject (bool ok, const juce::String& error = {},
                            const juce::String& path = {})
    {
        auto* obj = new juce::DynamicObject();
        obj->setProperty ("ok",    ok);
        obj->setProperty ("error", error);
        if (path.isNotEmpty()) obj->setProperty ("path", path);
        return juce::var (obj);
    }
}

void GenVstAudioProcessorEditor::doLoadPatch (const juce::Array<juce::var>& args,
                                              juce::WebBrowserComponent::NativeFunctionCompletion completion)
{
    const auto path = stringArg (args, 0);
    const auto err  = processor.loadPresetFromPath (path);
    if (err.isNotEmpty())
        emitToast ("error", err);
    completion (resultObject (err.isEmpty(), err, path));
}

void GenVstAudioProcessorEditor::doSavePatch (const juce::Array<juce::var>& args,
                                              juce::WebBrowserComponent::NativeFunctionCompletion completion)
{
    auto name = stringArg (args, 0);
    if (name.isEmpty()) name = "preset";

    juce::String saveErr;
    const auto savedPath = processor.savePresetForCurrentMode (name, saveErr);
    if (savedPath.isEmpty())
    {
        emitToast ("error", saveErr);
        completion (resultObject (false, saveErr));
        return;
    }
    emitToast ("info", "Saved " + name);
    completion (resultObject (true, {}, savedPath));
}

void GenVstAudioProcessorEditor::doImportPatch (const juce::Array<juce::var>& /*args*/,
                                                juce::WebBrowserComponent::NativeFunctionCompletion completion)
{
    const auto filter = juce::String (buildPatchExtensionFilter());
    fileChooser = std::make_unique<juce::FileChooser> (
        "Import patch", juce::File{}, filter);

    fileChooser->launchAsync (juce::FileBrowserComponent::openMode
                              | juce::FileBrowserComponent::canSelectFiles,
        [this, completion = std::move (completion)] (const juce::FileChooser& fc)
        {
            const auto file = fc.getResult();
            if (file == juce::File{}) { completion (resultObject (false, "cancelled")); return; }

            const auto err = processor.getPatchBrowser().importPatchFile (file.getFullPathName());
            if (! err.empty())
            {
                emitToast ("error", juce::String (err));
                completion (resultObject (false, juce::String (err)));
                return;
            }
            emitToast ("info", "Imported " + file.getFileName());
            completion (resultObject (true, {}, file.getFullPathName()));
        });
}

void GenVstAudioProcessorEditor::doExportPatch (const juce::Array<juce::var>& args,
                                                juce::WebBrowserComponent::NativeFunctionCompletion completion)
{
    // `format` is "tfi" / "vgi" / "psg". The chooser default extension drives
    // the suggested filename.
    const auto format = stringArg (args, 0).toLowerCase();
    if (format.isEmpty())
    {
        completion (resultObject (false, "missing export format"));
        return;
    }

    juce::String filter;
    if      (format == "tfi") filter = "*.tfi";
    else if (format == "vgi") filter = "*.vgi";
    else if (format == "psg") filter = "*.psg";
    else
    {
        completion (resultObject (false, "unsupported format: " + format));
        return;
    }

    fileChooser = std::make_unique<juce::FileChooser> (
        "Export patch", juce::File{}, filter);

    fileChooser->launchAsync (juce::FileBrowserComponent::saveMode
                              | juce::FileBrowserComponent::warnAboutOverwriting,
        [this, format, completion = std::move (completion)] (const juce::FileChooser& fc)
        {
            auto file = fc.getResult();
            if (file == juce::File{}) { completion (resultObject (false, "cancelled")); return; }

            // Ensure the chosen file has the right extension (some platforms
            // skip auto-appending).
            const auto dotExt = juce::String (".") + format;
            if (! file.hasFileExtension (dotExt))
                file = file.withFileExtension (dotExt);

            const auto err = processor.exportPresetForCurrentMode (file.getFullPathName());
            if (err.isNotEmpty())
            {
                emitToast ("error", err);
                completion (resultObject (false, err));
                return;
            }
            emitToast ("info", "Exported " + file.getFileName());
            completion (resultObject (true, {}, file.getFullPathName()));
        });
}

void GenVstAudioProcessorEditor::doAddPatchRoot (const juce::Array<juce::var>& /*args*/,
                                                  juce::WebBrowserComponent::NativeFunctionCompletion completion)
{
    fileChooser = std::make_unique<juce::FileChooser> (
        "Add patch folder", juce::File{}, "*");

    fileChooser->launchAsync (juce::FileBrowserComponent::openMode
                              | juce::FileBrowserComponent::canSelectDirectories,
        [this, completion = std::move (completion)] (const juce::FileChooser& fc)
        {
            const auto file = fc.getResult();
            if (file == juce::File{} || ! file.isDirectory())
            {
                completion (resultObject (false, "cancelled"));
                return;
            }
            const auto id = processor.getPatchBrowser().addCustomRoot (file.getFullPathName());
            if (id.isEmpty())
            {
                emitToast ("warn", "Folder already registered or not a directory");
                completion (resultObject (false, "addCustomRoot failed"));
                return;
            }
            emitToast ("info", "Added folder " + file.getFileName());
            completion (resultObject (true, {}, file.getFullPathName()));
        });
}

void GenVstAudioProcessorEditor::doDeletePatch (const juce::Array<juce::var>& args,
                                                juce::WebBrowserComponent::NativeFunctionCompletion completion)
{
    const auto path = stringArg (args, 0);
    if (path.isEmpty())
    {
        completion (resultObject (false, "empty path"));
        return;
    }
    const auto err = processor.getPatchBrowser().deletePatchFile (path);
    if (! err.empty())
    {
        emitToast ("warn", juce::String (err));
        completion (resultObject (false, juce::String (err)));
        return;
    }
    completion (resultObject (true, {}, path));
}

void GenVstAudioProcessorEditor::doPatchNav (const juce::Array<juce::var>& args,
                                              juce::WebBrowserComponent::NativeFunctionCompletion completion)
{
    const int dir = intArg (args, 0, 0);
    const auto err = processor.patchNavigate (dir);
    if (err.isNotEmpty())
    {
        // No-op cases (D mode, empty pool) shouldn't toast — just return.
        completion (resultObject (false, err));
        return;
    }
    completion (resultObject (true));
}

void GenVstAudioProcessorEditor::doExpandFolder (const juce::Array<juce::var>& args,
                                                  juce::WebBrowserComponent::NativeFunctionCompletion completion)
{
    const auto path = stringArg (args, 0);
    completion (processor.getPatchBrowser().folderAsJson (path));
}

void GenVstAudioProcessorEditor::doGetPatchList (const juce::Array<juce::var>& /*args*/,
                                                  juce::WebBrowserComponent::NativeFunctionCompletion completion)
{
    completion (processor.listAllPresetsAsJson());
}

void GenVstAudioProcessorEditor::doGetPatchRoots (const juce::Array<juce::var>& /*args*/,
                                                   juce::WebBrowserComponent::NativeFunctionCompletion completion)
{
    completion (processor.getPatchBrowser().rootsAsJson());
}

// -----------------------------------------------------------------------------
// FileDragAndDropTarget (Task 09 — drag-and-drop import)
// -----------------------------------------------------------------------------

bool GenVstAudioProcessorEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    namespace fs = std::filesystem;
    for (const auto& f : files)
    {
        const fs::path p { f.toRawUTF8() };
        std::error_code ec;
        if (fs::is_directory (p, ec)) return true;
        const auto ext = juce::String (p.extension().string()).toLowerCase();
        if (isSupportedPatchExtension (ext.toRawUTF8())) return true;
        if (ext == ".vgm" || ext == ".vgz") return true;
    }
    return false;
}

void GenVstAudioProcessorEditor::filesDropped (const juce::StringArray& files, int, int)
{
    std::vector<juce::String> paths;
    paths.reserve ((std::size_t) files.size());
    for (const auto& f : files) paths.push_back (f);
    handleDroppedPaths (paths);
}

void GenVstAudioProcessorEditor::handleDroppedPaths (const std::vector<juce::String>& paths)
{
    namespace fs = std::filesystem;
    auto& browser = processor.getPatchBrowser();

    int importedFiles  = 0;
    int importedBanks  = 0;
    int importedFolders = 0;
    int skipped        = 0;
    std::vector<std::string> errors;

    for (const auto& path : paths)
    {
        const fs::path p { path.toRawUTF8() };
        std::error_code ec;

        if (fs::is_directory (p, ec))
        {
            // Folder drop: recursive import into the user-imported root
            // (ADR-0025 + v2 spec — "register as custom root" was the v1
            // behaviour; v2 imports every supported file).
            const auto res = browser.importPatchFolder (path);
            importedFiles += res.imported;
            skipped       += res.skipped;
            for (const auto& e : res.errors) errors.push_back (e);
            ++importedFolders;
            continue;
        }

        const auto extLower = juce::String (p.extension().string()).toLowerCase();
        if (extLower == ".vgm" || extLower == ".vgz")
        {
            // Import Bank — extract every patch and write to the user-imported root.
            std::string vgmErr;
            const auto patches = extractFmPatches (p, vgmErr);
            if (! vgmErr.empty())
            {
                errors.push_back (vgmErr);
                continue;
            }
            const auto saveResult = browser.saveExtractedPatches (patches);
            importedBanks += saveResult.saved;
            for (const auto& e : saveResult.errors) errors.push_back (e);
            (void) browser.rescanWritableRoots();
            continue;
        }

        if (isSupportedPatchExtension (extLower.toRawUTF8()))
        {
            const auto err = browser.importPatchFile (path);
            if (! err.empty()) errors.push_back (err);
            else               ++importedFiles;
            continue;
        }

        // Unrecognised extension — silently ignored per the task spec.
        ++skipped;
    }

    // Summary toast.
    juce::StringArray pieces;
    if (importedFiles  > 0) pieces.add (juce::String (importedFiles)  + " file(s)");
    if (importedBanks  > 0) pieces.add (juce::String (importedBanks)  + " bank patch(es)");
    if (importedFolders> 0) pieces.add (juce::String (importedFolders)+ " folder(s)");
    if (! pieces.isEmpty())
        emitToast ("info", "Imported " + pieces.joinIntoString (", "));
    if (! errors.empty())
        emitToast ("warn",
                   juce::String ((int) errors.size()) + " drop error(s); first: "
                       + juce::String (errors.front()));
}
