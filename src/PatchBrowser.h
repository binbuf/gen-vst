#pragma once

#include <array>
#include <atomic>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <juce_core/juce_core.h>

#include "PatchSystem.h"

namespace genvst
{

// The four flavours of patch root the browser organises (ADR-0006 +
// 04-patch-system.md *Patch roots*):
//   Factory       — bundled patches; read-only, auto-loaded at startup.
//   UserSaved     — <userAppData>/GenVst/patches/saved/;    writable.
//                   Populated only by savePatchAsTfi(). Feeds the PRESETS tab.
//   UserImported  — <userAppData>/GenVst/patches/imported/; writable.
//                   Populated only by importPatchFile() + drag-and-drop. Feeds
//                   the IMPORT tab.
//   Custom        — any user-registered folder; paths persisted in plugin state.
enum class PatchRootKind
{
    Factory,
    UserSaved,
    UserImported,
    Custom
};

// One patch file inside a folder. `path` is the absolute filesystem path and
// also serves as the file's stable id for the UI.
struct PatchEntry
{
    juce::String name;     // filename stem (display)
    juce::String path;     // absolute path (stable id)
};

// A node in the lazy folder tree. The root scan only walks each PatchRoot's
// immediate children, so subfolder nodes start with `scanned == false` and
// `patchCount == -1`; both fields fill in once the user expands the node.
// `path` (absolute) doubles as the folder's stable id.
struct PatchFolder
{
    juce::String                              name;
    juce::String                              path;
    bool                                      scanned    = false;
    int                                       patchCount = -1;
    std::vector<std::unique_ptr<PatchFolder>> subfolders;   // alphabetical
    std::vector<PatchEntry>                   patches;      // alphabetical
};

// A patch root: one navigable tree, rooted at `folder`. The `id` is stable for
// the lifetime of the process and is what the UI sends back in
// removeCustomRoot / getPatchList calls.
struct PatchRoot
{
    PatchRootKind                kind;
    juce::String                 id;
    juce::String                 displayName;
    bool                         writable;
    std::unique_ptr<PatchFolder> folder;
};

// The C++ backend for the folder-tree patch browser (Task 09 / ADR-0006).
//
// Owns: the three kinds of root, the lazy folder tree, the background
// name-search index, the lock-free SPSC (part, Patch) delivery queue to the
// audio thread, and the factory-patches list Program Change indexes.
//
// Thread model:
//  - Folder-tree mutation (expand, custom roots) and patch loading run on the
//    message thread.
//  - The search index is rebuilt on a background thread (juce::Thread).
//  - The audio thread is read-only against:
//        * `factoryPatches` (populated once at initialize() and not touched
//          afterwards);
//        * the SPSC delivery queue (it is the sole consumer).
class PatchBrowser : private juce::Thread
{
public:
    // The audio-thread delivery queue size; per-click loads push a single
    // entry the audio thread drains every block (04-patch-system.md
    // "Audio Thread Delivery").
    static constexpr int kQueueCapacity = 8;

    // Slot count for the per-"part" active-path table. The v1 multitimbral
    // model is gone (mvp2/02-strip-v1) — Task 09 reshapes this surface as
    // part of the tagged preset browser. Kept as a small fixed pool so the
    // existing per-part API (loadIntoPart / activePatchPath) compiles
    // unchanged until Task 09 lands.
    static constexpr int kNumPartSlots = 6;

    PatchBrowser();
    ~PatchBrowser() override;

    PatchBrowser (const PatchBrowser&)            = delete;
    PatchBrowser& operator= (const PatchBrowser&) = delete;

    // -------------------------------------------------------------------------
    // Lifecycle
    // -------------------------------------------------------------------------

    // Initialise with a resolved factory root path (the caller resolves it —
    // ADR-0005: plugin bundle vs standalone data dir). Ensures the user root
    // exists, scans each root's immediate children, populates `factoryPatches`
    // by loading the factory root's top-level files, and starts the background
    // search-index thread. Idempotent — repeat calls are no-ops.
    //
    // Passing an empty path is allowed (no factory bank shipped); the factory
    // root then has zero patches and an unscanned folder.
    void initialize (const std::filesystem::path& factoryRoot);

    // Stop the background thread (idempotent). Called from the destructor; can
    // also be invoked explicitly during teardown.
    void shutdown();

    // -------------------------------------------------------------------------
    // Folder tree (message thread)
    // -------------------------------------------------------------------------

    const std::vector<std::unique_ptr<PatchRoot>>& roots() const noexcept { return rootList; }

    // Ensure `folder.path`'s immediate children are scanned; no-op if already.
    // Counts and subfolder lists are filled in on this call.
    void expandFolder (PatchFolder& folder);

    // Lookup a folder anywhere in any root by its absolute-path id. Returns
    // nullptr if no folder with that path exists (it might never have been
    // scanned). Message thread only.
    PatchFolder* findFolderByPath (const juce::String& absolutePath);

    // -------------------------------------------------------------------------
    // Custom roots (message thread)
    // -------------------------------------------------------------------------

    // Register `absolutePath` as a custom root. The root's immediate children
    // are scanned eagerly; deeper folders stay lazy. On success returns the new
    // root id; on failure (path is not a directory, already registered) returns
    // an empty string.
    juce::String addCustomRoot (const juce::String& absolutePath);

    // Unregister a custom root by id. Files on disk are never touched.
    bool removeCustomRoot (const juce::String& rootId);

    // -------------------------------------------------------------------------
    // Background search
    // -------------------------------------------------------------------------

    struct SearchHit
    {
        juce::String name;          // filename stem
        juce::String path;          // absolute path
        juce::String rootId;
        juce::String folderPath;    // path of the containing folder
    };

    // Case-insensitive substring search against the background index. Never
    // triggers a full filesystem scan; returns whatever the indexer has
    // produced so far. Thread-safe (uses an internal mutex).
    std::vector<SearchHit> search (const juce::String& query,
                                   int                 maxResults = 200) const;

    bool isSearchIndexReady() const noexcept { return indexBuilt.load (std::memory_order_acquire); }

    // -------------------------------------------------------------------------
    // Patch loading (message thread -> audio thread)
    // -------------------------------------------------------------------------

    // Parse the patch at `absolutePath` and push (part, Patch) into the
    // audio-thread delivery queue. Returns empty on success; a non-empty
    // string on parse failure (a corrupt file or wrong extension), in which
    // case the audio thread sees nothing — the load failure is purely a
    // message-thread concern (04-patch-system.md "Audio Thread Delivery").
    std::string loadIntoPart (int part, const juce::String& absolutePath);

    // The current "active patch path" for `part`. Empty until a patch is
    // loaded into this part via loadIntoPart (or until a PC-driven path is
    // wired in by Task 06's PC handler). Exposed for Task 16's state save.
    juce::String activePatchPath (int part) const;

    // Clear the recorded active patch path for `part`. Used by the
    // resetCurrentPart / resetAllToDefaults native functions so that after a
    // reset the UI does not still show "this part is on patch X".
    void clearActivePatchPath (int part);

    // Re-scan the user-saved + user-imported roots from disk. Used by the
    // cross-instance refresh path (PluginEditor mtime-poll Timer) so an
    // import done in plugin instance A propagates to instance B without the
    // user having to reopen the editor. Returns true if any root's
    // patchCount differs from the cached value (i.e. files were added or
    // removed by some other process / instance). Message thread only.
    bool rescanWritableRoots();

    // Last-modified time for the user-saved / user-imported root directories
    // (milliseconds since epoch). 0 if the directory does not exist. Used by
    // the editor's mtime poll to detect changes from other instances cheaply.
    std::int64_t userSavedRootMtime() const;
    std::int64_t userImportedRootMtime() const;

    // -------------------------------------------------------------------------
    // Audio-thread queue drain
    // -------------------------------------------------------------------------

    // Pop every pending delivery in FIFO order and pass it to `apply(part,
    // patch)`. Lock-free, allocation-free. Audio thread only.
    template <typename Fn>
    void drainAudioThreadQueue (Fn&& apply) noexcept
    {
        int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
        const int ready = deliveryFifo.getNumReady();
        deliveryFifo.prepareToRead (ready, start1, size1, start2, size2);

        for (int i = 0; i < size1; ++i)
            apply (deliveryPartSlots[(std::size_t) (start1 + i)],
                   deliveryPatchSlots[(std::size_t) (start1 + i)]);
        for (int i = 0; i < size2; ++i)
            apply (deliveryPartSlots[(std::size_t) (start2 + i)],
                   deliveryPatchSlots[(std::size_t) (start2 + i)]);

        deliveryFifo.finishedRead (size1 + size2);
    }

    // -------------------------------------------------------------------------
    // Factory patches (Program Change source of truth)
    // -------------------------------------------------------------------------

    int          numFactoryPatches() const noexcept;

    // Returns nullptr if `n` is out of range. The returned pointer is valid
    // for the lifetime of the PatchBrowser — the factory list is built once at
    // initialize() and not modified afterwards, so audio-thread reads are
    // safe (07-feature-spec.md "Program Change").
    const Patch* factoryPatchByIndex (int n) const noexcept;

    // -------------------------------------------------------------------------
    // Save / Import / Export (message thread)
    // -------------------------------------------------------------------------

    struct SaveResult
    {
        juce::String path;      // absolute path written, empty on failure
        std::string  error;     // empty on success
    };

    // Write `patch` to the user-saved root as `<name>.tfi`. Creates the saved
    // root if necessary. `name` is sanitised (replace OS-illegal chars with
    // '_'). 04-patch-system.md: savePatch() is the only path that populates
    // the PRESETS tab.
    SaveResult savePatchAsTfi (const Patch& patch, const juce::String& name);

    // Copy a file from `sourcePath` into the user-imported root (preserving
    // the file name). Used by the importPatch native function and by the
    // drag-and-drop import path. Returns empty on success or an error string
    // on failure.
    std::string importPatchFile (const juce::String& sourcePath);

    // Result of a recursive folder import.
    struct FolderImportResult
    {
        int          imported = 0;       // supported patch files copied
        int          skipped  = 0;       // non-patch files encountered
        std::vector<std::string> errors; // per-file copy failures
    };

    // Recursively copy every supported patch file (see kSupportedPatchExtensions
    // in PatchSystem.h) under `sourcePath` into the user-imported root,
    // preserving filenames (no directory hierarchy is re-created). Non-patch
    // files are silently skipped. Returns counts + per-file errors; never throws.
    FolderImportResult importPatchFolder (const juce::String& sourcePath);

    // Write `patch` to `destinationPath`, format selected by extension
    // (`.tfi` -> TFI, `.vgi` -> VGI). Used by the exportPatch native function.
    // Returns empty on success or an error string on failure.
    std::string exportPatchToPath (const Patch&        patch,
                                   const juce::String& destinationPath);

    // VGM bank-import sink (Task 21). Writes every Patch returned by
    // VgmExtract::extractFmPatches to the user-imported root as a `.tfi`
    // (filename = sanitiseFileName(patch.name) + ".tfi"), overwriting any
    // existing entry with the same name.
    //
    // This routine **does not** touch the folder tree or the search index, so
    // it is safe to call from the editor's background extraction thread. The
    // caller must follow up on the message thread with rescanWritableRoots()
    // (refresh the tree + invalidate the index) and emitPatchRootsChanged
    // (push a UI refresh event).
    struct VgmImportSaveResult
    {
        int                      saved = 0;
        std::vector<std::string> errors;
    };
    VgmImportSaveResult saveExtractedPatches (const std::vector<Patch>& patches);

    // Delete a patch file from a writable root (UserSaved / UserImported).
    // Refreshes the containing folder and schedules a search-index rebuild.
    // Returns empty on success or an error string on failure (path not in any
    // writable root, file does not exist, fs::remove failed).
    std::string deletePatchFile (const juce::String& absolutePath);

    // -------------------------------------------------------------------------
    // JSON helpers for the WebView native functions
    // -------------------------------------------------------------------------

    // [{kind, id, displayName, writable, path, scanned, patchCount,
    //   subfolders: [{path, name, scanned, patchCount}, ...]}, ...]
    juce::var rootsAsJson() const;

    // { path, name, scanned, patchCount,
    //   subfolders: [...], patches: [{path, name}, ...] }
    // If the folder hasn't been scanned yet, scans it before returning.
    juce::var folderAsJson (const juce::String& folderPath);

    // [{name, path, rootId, folderPath}, ...]
    juce::var searchAsJson (const juce::String& query) const;

private:
    // ---- Background thread (search-index builder) -------------------------
    void run() override;

    // ---- Filesystem helpers ----------------------------------------------
    // resolveUserRootBase() returns <userAppData>/GenVst/patches/. The two
    // writable subroots live under it as `saved/` and `imported/`.
    static std::filesystem::path resolveUserRootBase();
    static std::filesystem::path resolveUserSavedRoot();
    static std::filesystem::path resolveUserImportedRoot();
    static juce::String          rootIdFor (PatchRootKind, const juce::String& absolutePath);

    static bool                 isPatchFileName (const std::filesystem::path&);
    static juce::String         sanitiseFileName (const juce::String&);

    // ---- Tree management --------------------------------------------------
    void                  scanImmediateChildren (PatchFolder& folder);
    void                  addRoot (std::unique_ptr<PatchRoot> root);
    PatchFolder*          findFolderImpl (PatchFolder& start, const juce::String& path);

    // ---- Search index -----------------------------------------------------
    struct IndexEntry
    {
        juce::String name;
        juce::String path;
        juce::String rootId;
        juce::String folderPath;
    };

    void                       indexAllRoots();
    void                       indexFolder (const std::filesystem::path& folder,
                                            const juce::String&          rootId,
                                            std::vector<IndexEntry>&     out);

    // ---- JSON helpers -----------------------------------------------------
    static juce::var folderSummaryJson (const PatchFolder&);
    static juce::var folderFullJson    (const PatchFolder&);

    // ---- Data members -----------------------------------------------------
    std::vector<std::unique_ptr<PatchRoot>> rootList;

    // Lock-free SPSC delivery queue. Producer = message thread (loadIntoPart);
    // consumer = audio thread (drainAudioThreadQueue). Capacity 4 ring.
    juce::AbstractFifo                            deliveryFifo { kQueueCapacity };
    std::array<int,   (std::size_t) kQueueCapacity> deliveryPartSlots {};
    std::array<Patch, (std::size_t) kQueueCapacity> deliveryPatchSlots {};

    // Factory patches, sorted by filename. Populated at initialize(); read-only
    // afterwards so the audio thread (Program Change handler) can read with no
    // synchronisation (07-feature-spec.md "Program Change").
    std::vector<Patch> factoryPatches;

    // Per-part active patch path (the file path each part was loaded from).
    // Set by loadIntoPart on the message thread, read by state-save (Task 16).
    std::array<juce::String, kNumPartSlots> activePaths;
    mutable juce::CriticalSection           activePathLock;

    // Search index — protected by indexMutex; populated by the background
    // thread. `indexBuilt` is the cheap "is it ready yet?" probe.
    mutable std::mutex      indexMutex;
    std::vector<IndexEntry> searchIndex;
    std::atomic<bool>       indexBuilt { false };

    bool initialised = false;
};

} // namespace genvst
