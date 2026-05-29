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

// GENVST_DIAG: instrumentation for the Ableton/Windows DPI whitespace bug.
// Enabled via `cmake -DGENVST_DIAG=ON`. Writes a labelled snapshot of JUCE +
// Win32 + WebView measurements to ~/Documents/GenVst-diag.log so the actual
// values seen in the host can be inspected without devtools. Compiled out
// completely when the option is off (default).
#if defined(GENVST_DIAG) && GENVST_DIAG
 #include <mutex>
 #if JUCE_WINDOWS
  #include <windows.h>
 #endif
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

#if JUCE_WINDOWS
namespace
{
    // Windows per-thread DPI virtualization workaround. When a DPI-unaware
    // thread (which is what JUCE's VST3 message thread inherits from Ableton's
    // process context) calls GetClientRect on an HWND that lives in a higher-
    // DPI display, Windows returns VIRTUALIZED (down-scaled) coordinates.
    // E.g. a real 1800x990 HWND at 150% display scale comes back as 1200x660.
    //
    // The fix is to temporarily switch this thread to PerMonitorV2 so the
    // Win32 query returns true physical pixels, then restore the previous
    // context so the rest of the thread's behaviour is unchanged.
    bool getPhysicalClientRect (HWND hwnd, RECT& out)
    {
        const auto prev = ::SetThreadDpiAwarenessContext (DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        const bool ok = ::GetClientRect (hwnd, &out) != FALSE;
        if (prev != nullptr)
            ::SetThreadDpiAwarenessContext (prev);
        return ok;
    }

    bool getPhysicalWindowRect (HWND hwnd, RECT& out)
    {
        const auto prev = ::SetThreadDpiAwarenessContext (DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        const bool ok = ::GetWindowRect (hwnd, &out) != FALSE;
        if (prev != nullptr)
            ::SetThreadDpiAwarenessContext (prev);
        return ok;
    }
}
#endif

// --- GENVST_DIAG helpers (no-op when option is off) ------------------------
#if defined(GENVST_DIAG) && GENVST_DIAG
namespace
{
    void initDiagLogger()
    {
        static std::once_flag flag;
        std::call_once (flag, []
        {
            const auto logFile = juce::File::getSpecialLocation (
                                     juce::File::userDocumentsDirectory)
                                 .getChildFile ("GenVst-diag.log");
            juce::Logger::setCurrentLogger (new juce::FileLogger (
                logFile,
                "Gen VST diagnostic log (built " __DATE__ " " __TIME__ ")",
                1024 * 1024));
        });
    }

    void logDiagSnapshot (const juce::String& tag, juce::Component& c)
    {
        juce::String msg = "DIAG[" + tag + "]";
        msg << " compW=" << c.getWidth() << " compH=" << c.getHeight();
        msg << " desktopGlobalScale="
            << juce::Desktop::getInstance().getGlobalScaleFactor();

        if (auto* peer = c.getPeer())
        {
            msg << " peerScale=" << peer->getPlatformScaleFactor();
           #if JUCE_WINDOWS
            auto* editorHwnd = (HWND) peer->getNativeHandle();
            if (editorHwnd != nullptr)
            {
                msg << " editorHwnd=0x" << juce::String::toHexString (
                                              (juce::pointer_sized_int) editorHwnd);

                RECT r;
                if (getPhysicalClientRect (editorHwnd, r))
                    msg << " editorClient=" << (int) (r.right - r.left)
                        << "x" << (int) (r.bottom - r.top);
                if (getPhysicalWindowRect (editorHwnd, r))
                    msg << " editorWindow=" << (int) (r.right - r.left)
                        << "x" << (int) (r.bottom - r.top);
                msg << " editorDpi=" << (int) ::GetDpiForWindow (editorHwnd);

                if (auto* parentHwnd = ::GetParent (editorHwnd))
                {
                    msg << " parentHwnd=0x" << juce::String::toHexString (
                                                  (juce::pointer_sized_int) parentHwnd);
                    if (getPhysicalClientRect (parentHwnd, r))
                        msg << " parentClient=" << (int) (r.right - r.left)
                            << "x" << (int) (r.bottom - r.top);
                    if (getPhysicalWindowRect (parentHwnd, r))
                        msg << " parentWindow=" << (int) (r.right - r.left)
                            << "x" << (int) (r.bottom - r.top);
                    msg << " parentDpi=" << (int) ::GetDpiForWindow (parentHwnd);
                }
            }
           #endif // JUCE_WINDOWS
        }
        else
        {
            msg << " (no-peer)";
        }

        juce::Logger::writeToLog (msg);
    }
}
#else
namespace
{
    inline void initDiagLogger() {}
    inline void logDiagSnapshot (const juce::String&, juce::Component&) {}
}
#endif // GENVST_DIAG

GenVstAudioProcessorEditor::GenVstAudioProcessorEditor (GenVstAudioProcessor& p)
    : juce::AudioProcessorEditor (&p),
      processor (p)
{
    initDiagLogger();
    logDiagSnapshot ("ctor-entry", *this);

    // ADR-0023 -- fixed 1200x660 editor (100px added for on-screen keyboard strip).
    setSize (1200, 660);
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

    // Build the per-apvts-parameter relays exhaustively. The previous code
    // declared named relays for only the header / Settings / Gallery params;
    // the per-mode panel params (FM operator/part, SQ channel, D mono/dry)
    // had no relays, so `bindSlider/bindToggle/bindCombo` on the JS side
    // ran against phantom SliderState objects — `setValueNotifyingHost`
    // from a preset load reached the audio thread but never echoed back
    // to the UI, leaving the panel knobs stuck while the sound changed.
    //
    // AudioParameterBool inherits from AudioParameterChoice in JUCE 8 —
    // test the more specific Bool first.
    for (auto* param : processor.getParameters())
    {
        auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (param);
        if (withId == nullptr) continue;
        const juce::String id = withId->paramID;

        if (dynamic_cast<juce::AudioParameterBool*> (param) != nullptr)
            toggleRelays.push_back ({ id, std::make_unique<juce::WebToggleButtonRelay> (id) });
        else if (dynamic_cast<juce::AudioParameterChoice*> (param) != nullptr)
            comboRelays.push_back  ({ id, std::make_unique<juce::WebComboBoxRelay>     (id) });
        else
            sliderRelays.push_back ({ id, std::make_unique<juce::WebSliderRelay>       (id) });
    }

    tryInitWebView();
    logDiagSnapshot ("ctor-after-init-webview", *this);

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
    logDiagSnapshot ("makeOptions-entry", *this);

    auto options = juce::WebBrowserComponent::Options{}
        .withBackend (juce::WebBrowserComponent::Options::Backend::webview2)
        .withWinWebView2Options (juce::WebBrowserComponent::Options::WinWebView2{}
            // Plugin processes are denied the default WebView2 user-data
            // location; point it at the temp dir or it can fail to initialise.
            .withUserDataFolder (juce::File::getSpecialLocation (
                juce::File::SpecialLocationType::tempDirectory))
            .withBackgroundColour (juce::Colour (0xff1c1f24)))
        .withNativeIntegrationEnabled();

    // Register every apvts-parameter relay the editor owns. The relay
    // vectors were populated in the editor constructor by iterating
    // processor.getParameters(); each Options.withOptionsFrom call returns
    // a new Options object that captures the relay reference, so we chain
    // through the existing local.
    for (auto& s : sliderRelays) options = options.withOptionsFrom (*s.relay);
    for (auto& t : toggleRelays) options = options.withOptionsFrom (*t.relay);
    for (auto& c : comboRelays)  options = options.withOptionsFrom (*c.relay);

    options = options
        .withEventListener ("uiReady", [this] (juce::var payload)
        {
            // Carries the JS-side init() outcome: { ok: true } on success,
            // or { ok: false, error: "...", stack: "..." } when the
            // permanent try/catch in ui/src/main.js caught a mount throw.
            // Logging the payload here turns an otherwise-silent JS error
            // into a one-line entry in the host's juce::Logger output.
            juce::Logger::writeToLog ("Gen VST: uiReady received from WebView: "
                                      + juce::JSON::toString (payload));

           #if defined(GENVST_DIAG) && GENVST_DIAG
            logDiagSnapshot ("uiReady", *this);
            if (webView != nullptr)
                webView->emitEventIfBrowserIsVisible ("requestDiag", juce::var{});
           #endif

            // Synthesize a patchLoaded event for whatever's currently active
            // in the processor. Without this, a cold-start default-preset
            // load (which fires in prepareToPlay before the editor exists)
            // updates apvts but the header LCD never hears about the patch
            // name — it sits on its "— EMPTY —" placeholder even though a
            // real patch is loaded. Reopening the editor on a project where
            // a preset was already loaded has the same shape.
            const auto mode = processor.currentMode();
            if (mode != GenVstAudioProcessor::Mode::D)
            {
                const auto path = processor.activePathForMode (mode);
                if (path.isNotEmpty())
                {
                    PatchLoadedNotifier note;
                    note.name = juce::String (
                        std::filesystem::path { path.toRawUTF8() }.stem().string());
                    note.tag  = (mode == GenVstAudioProcessor::Mode::FM) ? Tag::FM : Tag::SQ;
                    note.path = path;
                    emitPatchLoaded (note);
                }
            }
        })
       #if defined(GENVST_DIAG) && GENVST_DIAG
        .withEventListener ("diagResponse", [this] (juce::var payload)
        {
            juce::Logger::writeToLog ("DIAG[js] " + juce::JSON::toString (payload));
            logDiagSnapshot ("after-diagResponse", *this);
        })
       #endif
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
            { doGetPatchRoots (args, std::move (completion)); })
        // On-screen keyboard: inject synthetic note events into the audio thread.
        .withNativeFunction ("noteOn",
            [this] (const juce::Array<juce::var>& args,
                    juce::WebBrowserComponent::NativeFunctionCompletion completion)
            {
                const int pitch    = args.size() > 0 ? static_cast<int> (args[0]) : -1;
                const int velocity = args.size() > 1 ? static_cast<int> (args[1]) : 100;
                if (pitch >= 0 && pitch <= 127)
                    processor.injectNoteOn (pitch, juce::jlimit (1, 127, velocity));
                completion (juce::var{});
            })
        .withNativeFunction ("noteOff",
            [this] (const juce::Array<juce::var>& args,
                    juce::WebBrowserComponent::NativeFunctionCompletion completion)
            {
                const int pitch = args.size() > 0 ? static_cast<int> (args[0]) : -1;
                if (pitch >= 0 && pitch <= 127)
                    processor.injectNoteOff (pitch);
                completion (juce::var{});
            });

   #if ! GENVST_DEV_SERVER
    options = options.withResourceProvider (
        [] (const auto& url) { return getWebResource (url); });
   #endif

    return options;
}

void GenVstAudioProcessorEditor::tryInitWebView()
{
    // Tear down any previous attempt (Retry path). Attachments must drop
    // before the WebView so the relay ↔ apvts bridge unwinds cleanly; the
    // relays themselves persist (they were built in the ctor) so a Retry
    // can re-attach against them.
    sliderAtts.clear();
    toggleAtts.clear();
    comboAtts.clear();
    uiScaleListener.reset();
    keyboardVisibleListener.reset();
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

    // Build one attachment per registered relay. apvts.getParameter() looks
    // up the parameter that owns the matching ID; the ctor seeded the relay
    // vectors directly from processor.getParameters(), so every relay has a
    // backing parameter and the null-check below is defensive only.
    for (auto& s : sliderRelays)
        if (auto* param = apvts.getParameter (s.id))
            sliderAtts.push_back (std::make_unique<juce::WebSliderParameterAttachment> (
                *param, *s.relay, apvts.undoManager));
    for (auto& t : toggleRelays)
        if (auto* param = apvts.getParameter (t.id))
            toggleAtts.push_back (std::make_unique<juce::WebToggleButtonParameterAttachment> (
                *param, *t.relay, apvts.undoManager));
    for (auto& c : comboRelays)
        if (auto* param = apvts.getParameter (c.id))
            comboAtts.push_back (std::make_unique<juce::WebComboBoxParameterAttachment> (
                *param, *c.relay, apvts.undoManager));

    // UI scale side-effect listener — pulls the choice index, applies an
    // integer zoom to the editor host bounds (ADR-0017 + ADR-0023). This
    // is independent of the combo relay/attachment built in the loop
    // above; the relay handles the UI ↔ apvts bridge, this drives the
    // resize side-effect.
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

    // Keyboard-visibility side-effect listener — resizes the editor window
    // when the user toggles "Show Keyboard" in Settings.
    if (auto* kbParam = apvts.getParameter ("keyboard_visible"))
    {
        keyboardVisibleListener = std::make_unique<juce::ParameterAttachment> (
            *kbParam,
            [this] (float normalised)
            {
                applyKeyboardVisible (normalised > 0.5f);
            },
            apvts.undoManager);
        keyboardVisibleListener->sendInitialUpdate();
    }

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
    currentUiScale = juce::jlimit (1, 3, n);
    applyWindowSize();
}

void GenVstAudioProcessorEditor::applyKeyboardVisible (bool visible)
{
    keyboardVisible = visible;
    applyWindowSize();
}

void GenVstAudioProcessorEditor::applyWindowSize()
{
    // ADR-0017 + ADR-0023. Base dimensions: 1200 wide, 660 tall with keyboard
    // strip or 560 without. Scale multiplied for 2×/3× zoom.
    const int baseH = keyboardVisible ? 660 : 560;
    setSize (1200 * currentUiScale, baseH * currentUiScale);
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

void GenVstAudioProcessorEditor::parentSizeChanged()
{
   #if defined(GENVST_DIAG) && GENVST_DIAG
    {
        juce::String msg = "DIAG[parentSizeChanged]";
        msg << " selfW=" << getWidth() << " selfH=" << getHeight();
        if (auto* p = getParentComponent())
            msg << " parentW=" << p->getWidth() << " parentH=" << p->getHeight();
        else
            msg << " (no-parent)";
       #if JUCE_WINDOWS
        if (auto* peer = getPeer())
        {
            if (auto* hwnd = (HWND) peer->getNativeHandle())
            {
                RECT r;
                if (GetClientRect (hwnd, &r))
                    msg << " editorClient=" << (int) (r.right - r.left)
                        << "x" << (int) (r.bottom - r.top);
            }
        }
       #endif
        juce::Logger::writeToLog (msg);
    }
   #endif

    syncToHostSize ("parentSizeChanged");
}

void GenVstAudioProcessorEditor::syncToHostSize (const char* origin)
{
   #if JUCE_WINDOWS
   #if defined(GENVST_DIAG) && GENVST_DIAG
    // First-call-of-this-origin log so we can confirm the entry point is
    // wired. Rate-limited (one entry per second) to avoid drowning the log
    // at 30Hz.
    static juce::int64 lastTraceMs = 0;
    const auto nowMs = juce::Time::getMillisecondCounter();
    const bool shouldTrace = (juce::int64) nowMs - lastTraceMs > 1000;
    if (shouldTrace)
    {
        lastTraceMs = (juce::int64) nowMs;
        juce::String trace = juce::String ("DIAG[syncToHostSize/") + origin + "/trace]";
        trace << " webView=" << (webView != nullptr ? "ok" : "NULL");
        if (auto* peer = getPeer())
        {
            trace << " peerScale=" << peer->getPlatformScaleFactor();
            if (auto* hwnd = (HWND) peer->getNativeHandle())
            {
                trace << " hwnd=0x" << juce::String::toHexString ((juce::pointer_sized_int) hwnd);
                RECT r;
                if (getPhysicalClientRect (hwnd, r))
                    trace << " physW=" << (int) (r.right - r.left)
                          << " physH=" << (int) (r.bottom - r.top);
                else
                    trace << " GetClientRect=FAILED";
            }
            else
                trace << " hwnd=NULL";
        }
        else
            trace << " peer=NULL";
        if (webView != nullptr)
            trace << " webViewW=" << webView->getWidth()
                  << " webViewH=" << webView->getHeight();
        juce::Logger::writeToLog (trace);
    }
   #endif

    if (webView == nullptr)
        return;

    if (auto* peer = getPeer())
    {
        const auto peerScale = peer->getPlatformScaleFactor();
        if (peerScale < 1.05 && peerScale > 0.95)
        {
            if (auto* hwnd = (HWND) peer->getNativeHandle())
            {
                RECT r;
                if (getPhysicalClientRect (hwnd, r))
                {
                    const int physW = (int) (r.right - r.left);
                    const int physH = (int) (r.bottom - r.top);
                    if (physW > 0 && physH > 0
                        && (physW != webView->getWidth() || physH != webView->getHeight()))
                    {
                       #if defined(GENVST_DIAG) && GENVST_DIAG
                        juce::Logger::writeToLog (juce::String ("DIAG[syncToHostSize/")
                            + origin + "] resizing webView "
                            + juce::String (webView->getWidth()) + "x"
                            + juce::String (webView->getHeight()) + " -> "
                            + juce::String (physW) + "x" + juce::String (physH));
                       #endif
                        webView->setBounds (0, 0, physW, physH);
                    }
                }
            }
        }
    }
   #else
    juce::ignoreUnused (origin);
   #endif
}

void GenVstAudioProcessorEditor::resized()
{
    logDiagSnapshot ("resized", *this);

    const auto b = getLocalBounds();

    if (webView != nullptr)
    {
        webView->setBounds (b);
        syncToHostSize ("resized");   // Ableton Auto-Scale override (see fn).
    }

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

    // Fallback for JUCE not surfacing the host-side HWND resize via
    // parentSizeChanged/resized after editor attachment — poll the actual
    // HWND size on every telemetry tick and resize the WebView if needed.
    // No-op when sizes already match (see syncToHostSize for the guard).
    syncToHostSize ("timer");

    auto& t = processor.getTelemetry();
    auto* obj = new juce::DynamicObject();
    obj->setProperty ("peakL",  t.vuLeft());
    obj->setProperty ("peakR",  t.vuRight());
    obj->setProperty ("noteOn", t.noteOn());

    // Build activeNotes array from 128-bit note mask for on-screen keyboard.
    juce::Array<juce::var> activeNotes;
    const uint64_t lo = t.activeNotesLow();
    const uint64_t hi = t.activeNotesHigh();
    for (int i = 0; i < 64; ++i)
        if (lo & (uint64_t (1) << i)) activeNotes.add (juce::var (i));
    for (int i = 0; i < 64; ++i)
        if (hi & (uint64_t (1) << i)) activeNotes.add (juce::var (64 + i));
    obj->setProperty ("activeNotes", juce::var (activeNotes));

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
