#include "PatchBrowser.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <fstream>
#include <system_error>

namespace fs = std::filesystem;

namespace genvst
{

namespace
{
    // Patch file extensions the browser recognises. Delegates to PatchSystem's
    // isSupportedPatchExtension so the list lives at exactly one place
    // (kSupportedPatchExtensions in PatchSystem.h).
    bool isPatchExtension (const fs::path& p) noexcept
    {
        const auto ext = p.extension().string();
        return isSupportedPatchExtension (ext);
    }

    // Cross-platform path -> juce::String. std::filesystem::path's native
    // encoding is wide on Windows and UTF-8 elsewhere; using the right view
    // avoids garbled Unicode in user folder names.
    juce::String pathString (const fs::path& p)
    {
       #if JUCE_WINDOWS
        return juce::String (p.wstring().c_str());
       #else
        const auto s = p.string();
        return juce::String::fromUTF8 (s.data(), (int) s.size());
       #endif
    }

    juce::String stemString (const fs::path& p)
    {
        return pathString (p.stem());
    }

    // The inverse — juce::String -> fs::path. Mirrors the rules above.
    fs::path toFsPath (const juce::String& s)
    {
       #if JUCE_WINDOWS
        return fs::path { s.toWideCharPointer() };
       #else
        return fs::path { s.toRawUTF8() };
       #endif
    }

    // Single dispatch point so loadIntoPart and the indexer pick the right
    // parser for any patch file by extension. Lower-cases the input so the
    // comparison is case-insensitive on all platforms.
    PatchLoadResult dispatchLoad (const fs::path& path)
    {
        const auto rawExt = path.extension().string();
        std::string ext (rawExt.size(), '\0');
        std::transform (rawExt.begin(), rawExt.end(), ext.begin(),
                        [] (char c)
                        { return static_cast<char> (std::tolower ((unsigned char) c)); });
        if (ext == ".tfi") return loadTFI (path);
        if (ext == ".vgi") return loadVGI (path);
        if (ext == ".dmp") return loadDMP (path);
        if (ext == ".y12") return loadY12 (path);
        if (ext == ".opm") return loadOPM (path);
        return { std::nullopt, "unrecognised patch extension: " + rawExt };
    }
}

PatchBrowser::PatchBrowser()
    : juce::Thread ("Gen VST patch index")
{
}

PatchBrowser::~PatchBrowser()
{
    shutdown();
}

// =============================================================================
// Lifecycle
// =============================================================================

void PatchBrowser::initialize (const fs::path& factoryRoot)
{
    if (initialised)
        return;

    // -------- Factory root --------
    const auto factoryFs = factoryRoot;
    auto factory = std::make_unique<PatchRoot>();
    factory->kind        = PatchRootKind::Factory;
    factory->id          = "factory";
    factory->displayName = "Factory";
    factory->writable    = false;
    factory->folder      = std::make_unique<PatchFolder>();
    factory->folder->name = "Factory";
    factory->folder->path = pathString (factoryFs);
    scanImmediateChildren (*factory->folder);

    // Populate the audio-thread factoryPatches list from the factory root's
    // top-level .tfi files (sorted by filename, like Task 06's enumeration).
    if (fs::is_directory (factoryFs))
    {
        std::vector<fs::path> tfiFiles;
        std::error_code ec;
        for (const auto& entry : fs::directory_iterator (factoryFs, ec))
            if (entry.is_regular_file (ec) && isPatchExtension (entry.path()))
                tfiFiles.push_back (entry.path());

        std::sort (tfiFiles.begin(), tfiFiles.end(),
                   [] (const fs::path& a, const fs::path& b)
                   {
                       return a.filename() < b.filename();
                   });

        factoryPatches.reserve (tfiFiles.size());
        for (const auto& path : tfiFiles)
            if (auto r = dispatchLoad (path); r.patch.has_value())
                factoryPatches.push_back (std::move (*r.patch));
    }

    addRoot (std::move (factory));

    // -------- User-saved + User-imported roots --------
    // 04-patch-system.md *Patch roots*: the single writable user root is split
    // into two distinct subroots — `saved/` (filled by savePatch) and
    // `imported/` (filled by importPatch + drag-and-drop). Both are created
    // idempotently on every startup; no installer step required.
    const auto savedFs    = resolveUserSavedRoot();
    const auto importedFs = resolveUserImportedRoot();
    std::error_code mkdirEc;
    fs::create_directories (savedFs,    mkdirEc);
    fs::create_directories (importedFs, mkdirEc);

    auto saved = std::make_unique<PatchRoot>();
    saved->kind        = PatchRootKind::UserSaved;
    saved->id          = "user-saved";
    saved->displayName = "Saved";
    saved->writable    = true;
    saved->folder      = std::make_unique<PatchFolder>();
    saved->folder->name = "Saved";
    saved->folder->path = pathString (savedFs);
    scanImmediateChildren (*saved->folder);
    addRoot (std::move (saved));

    auto imported = std::make_unique<PatchRoot>();
    imported->kind        = PatchRootKind::UserImported;
    imported->id          = "user-imported";
    imported->displayName = "Imported";
    imported->writable    = true;
    imported->folder      = std::make_unique<PatchFolder>();
    imported->folder->name = "Imported";
    imported->folder->path = pathString (importedFs);
    scanImmediateChildren (*imported->folder);
    addRoot (std::move (imported));

    initialised = true;

    // Kick off the background indexer.
    indexBuilt.store (false, std::memory_order_release);
    startThread (juce::Thread::Priority::low);
}

void PatchBrowser::shutdown()
{
    if (isThreadRunning())
        stopThread (2000);
}

// =============================================================================
// Folder tree
// =============================================================================

void PatchBrowser::scanImmediateChildren (PatchFolder& folder)
{
    if (folder.scanned)
        return;

    folder.subfolders.clear();
    folder.patches.clear();

    const fs::path folderPath { folder.path.toRawUTF8() };
    std::error_code ec;

    if (! fs::is_directory (folderPath, ec))
    {
        folder.scanned    = true;
        folder.patchCount = 0;
        return;
    }

    for (const auto& entry : fs::directory_iterator (folderPath, ec))
    {
        std::error_code itemEc;
        if (entry.is_directory (itemEc))
        {
            auto sub = std::make_unique<PatchFolder>();
            sub->name = pathString (entry.path().filename());
            sub->path = pathString (entry.path());
            // Subfolders start unscanned — counts get filled in on expand.
            folder.subfolders.push_back (std::move (sub));
        }
        else if (entry.is_regular_file (itemEc) && isPatchExtension (entry.path()))
        {
            folder.patches.push_back ({ stemString (entry.path()),
                                        pathString (entry.path()) });
        }
    }

    // Alphabetical, case-insensitive, for stable UI ordering across OSes.
    auto byName = [] (const auto& a, const auto& b)
    {
        return a->name.compareIgnoreCase (b->name) < 0;
    };
    auto entryByName = [] (const PatchEntry& a, const PatchEntry& b)
    {
        return a.name.compareIgnoreCase (b.name) < 0;
    };

    std::sort (folder.subfolders.begin(), folder.subfolders.end(), byName);
    std::sort (folder.patches.begin(),    folder.patches.end(),    entryByName);

    folder.scanned    = true;
    folder.patchCount = static_cast<int> (folder.patches.size());
}

void PatchBrowser::expandFolder (PatchFolder& folder)
{
    scanImmediateChildren (folder);
}

PatchFolder* PatchBrowser::findFolderByPath (const juce::String& absolutePath)
{
    for (auto& root : rootList)
        if (auto* hit = findFolderImpl (*root->folder, absolutePath))
            return hit;
    return nullptr;
}

PatchFolder* PatchBrowser::findFolderImpl (PatchFolder& start, const juce::String& path)
{
    if (start.path == path)
        return &start;
    for (auto& sub : start.subfolders)
        if (auto* hit = findFolderImpl (*sub, path))
            return hit;
    return nullptr;
}

// =============================================================================
// Custom roots
// =============================================================================

juce::String PatchBrowser::addCustomRoot (const juce::String& absolutePath)
{
    const fs::path fsPath { absolutePath.toRawUTF8() };
    std::error_code ec;
    if (! fs::is_directory (fsPath, ec))
        return {};

    // Reject duplicates (same absolute path).
    for (const auto& r : rootList)
        if (r->kind == PatchRootKind::Custom && r->folder && r->folder->path == absolutePath)
            return {};

    auto root = std::make_unique<PatchRoot>();
    root->kind        = PatchRootKind::Custom;
    root->id          = rootIdFor (PatchRootKind::Custom, absolutePath);
    root->displayName = pathString (fsPath.filename());
    if (root->displayName.isEmpty())
        root->displayName = absolutePath;
    root->writable    = false;   // never delete from custom roots — read-only by policy
    root->folder      = std::make_unique<PatchFolder>();
    root->folder->name = root->displayName;
    root->folder->path = pathString (fsPath);
    scanImmediateChildren (*root->folder);

    const auto id = root->id;
    addRoot (std::move (root));

    // The new root invalidates the search index — schedule a rebuild.
    if (initialised)
    {
        indexBuilt.store (false, std::memory_order_release);
        if (! isThreadRunning())
            startThread (juce::Thread::Priority::low);
        else
            notify();
    }

    return id;
}

bool PatchBrowser::removeCustomRoot (const juce::String& rootId)
{
    for (auto it = rootList.begin(); it != rootList.end(); ++it)
    {
        if ((*it)->kind == PatchRootKind::Custom && (*it)->id == rootId)
        {
            rootList.erase (it);
            // Rebuild the index so search no longer returns hits from this root.
            indexBuilt.store (false, std::memory_order_release);
            if (! isThreadRunning())
                startThread (juce::Thread::Priority::low);
            else
                notify();
            return true;
        }
    }
    return false;
}

void PatchBrowser::addRoot (std::unique_ptr<PatchRoot> root)
{
    rootList.push_back (std::move (root));
}

// =============================================================================
// Search
// =============================================================================

void PatchBrowser::run()
{
    // Sleep briefly so startup doesn't compete with the foreground UI scan
    // for the disk on the first frame.
    wait (50);

    while (! threadShouldExit())
    {
        if (! indexBuilt.load (std::memory_order_acquire))
        {
            indexAllRoots();
            indexBuilt.store (true, std::memory_order_release);
        }

        // Wait until either we're asked to rebuild (notify()) or shutdown.
        wait (-1);
    }
}

void PatchBrowser::indexAllRoots()
{
    std::vector<IndexEntry> fresh;

    // Snapshot the roots we need to walk. Roots are added/removed on the
    // message thread; we capture a stable view of paths + ids here so the
    // walk doesn't have to hold any lock.
    struct RootSnapshot { juce::String id; juce::String path; };
    std::vector<RootSnapshot> snapshots;
    snapshots.reserve (rootList.size());
    for (const auto& r : rootList)
        if (r->folder)
            snapshots.push_back ({ r->id, r->folder->path });

    for (const auto& snap : snapshots)
    {
        if (threadShouldExit())
            return;

        const fs::path rootPath { snap.path.toRawUTF8() };
        std::error_code ec;
        if (! fs::is_directory (rootPath, ec))
            continue;
        indexFolder (rootPath, snap.id, fresh);
    }

    {
        std::lock_guard<std::mutex> lk (indexMutex);
        searchIndex = std::move (fresh);
    }
}

void PatchBrowser::indexFolder (const fs::path&          folder,
                                const juce::String&      rootId,
                                std::vector<IndexEntry>& out)
{
    // Recursive walk — but cooperative: bail out on stop request, and only
    // pay for what fits in memory. 30k entries is fine; gigabyte trees are
    // out of scope for the MVP.
    std::error_code ec;
    for (const auto& entry : fs::recursive_directory_iterator (
             folder,
             fs::directory_options::skip_permission_denied,
             ec))
    {
        if (threadShouldExit())
            return;

        std::error_code itemEc;
        if (! entry.is_regular_file (itemEc))
            continue;
        if (! isPatchExtension (entry.path()))
            continue;

        IndexEntry ie;
        ie.name       = stemString (entry.path());
        ie.path       = pathString (entry.path());
        ie.rootId     = rootId;
        ie.folderPath = pathString (entry.path().parent_path());
        out.push_back (std::move (ie));
    }
}

std::vector<PatchBrowser::SearchHit>
PatchBrowser::search (const juce::String& query, int maxResults) const
{
    std::vector<SearchHit> hits;
    if (query.isEmpty())
        return hits;

    const auto needle = query.toLowerCase();

    std::lock_guard<std::mutex> lk (indexMutex);
    hits.reserve (juce::jmin (maxResults, (int) searchIndex.size()));
    for (const auto& e : searchIndex)
    {
        if (e.name.toLowerCase().contains (needle))
        {
            hits.push_back ({ e.name, e.path, e.rootId, e.folderPath });
            if ((int) hits.size() >= maxResults)
                break;
        }
    }
    return hits;
}

// =============================================================================
// Patch loading (message thread -> audio thread)
// =============================================================================

std::string PatchBrowser::loadIntoPart (int part, const juce::String& absolutePath)
{
    if (part < 0 || part >= PartManager::kNumParts)
        return "invalid part index: " + std::to_string (part);

    const fs::path fsPath { absolutePath.toRawUTF8() };
    auto result = dispatchLoad (fsPath);
    if (! result.patch.has_value())
        return result.error.empty() ? std::string ("unknown load error") : result.error;

    // Push (part, patch) into the SPSC delivery queue. Producer-side string
    // assignment may allocate inside the patch slot — fine on the message
    // thread. If the audio thread is behind on draining, the queue can be
    // full; we return an error rather than blocking.
    int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
    deliveryFifo.prepareToWrite (1, start1, size1, start2, size2);
    if (size1 + size2 < 1)
        return "patch delivery queue full (audio thread busy)";

    const int slot = size1 > 0 ? start1 : start2;
    deliveryPartSlots[(std::size_t) slot]  = part;
    deliveryPatchSlots[(std::size_t) slot] = *result.patch;
    deliveryFifo.finishedWrite (1);

    // Record the active patch path for state save (Task 16).
    {
        const juce::ScopedLock lk (activePathLock);
        activePaths[(std::size_t) part] = absolutePath;
    }

    return {};
}

juce::String PatchBrowser::activePatchPath (int part) const
{
    if (part < 0 || part >= PartManager::kNumParts)
        return {};
    const juce::ScopedLock lk (activePathLock);
    return activePaths[(std::size_t) part];
}

void PatchBrowser::clearActivePatchPath (int part)
{
    if (part < 0 || part >= PartManager::kNumParts)
        return;
    const juce::ScopedLock lk (activePathLock);
    activePaths[(std::size_t) part] = juce::String{};
}

bool PatchBrowser::rescanWritableRoots()
{
    bool changed = false;
    for (auto& r : rootList)
    {
        if (! r) continue;
        if (r->kind != PatchRootKind::UserSaved
         && r->kind != PatchRootKind::UserImported)
            continue;
        if (r->folder == nullptr) continue;

        const int prevCount = r->folder->patchCount;
        r->folder->scanned  = false;
        r->folder->subfolders.clear();
        r->folder->patches.clear();
        scanImmediateChildren (*r->folder);
        if (r->folder->patchCount != prevCount)
            changed = true;
    }

    if (changed)
    {
        // Background search index is stale; schedule a rebuild on the worker
        // thread (matches the post-import path).
        indexBuilt.store (false, std::memory_order_release);
        if (isThreadRunning()) notify();
        else                   startThread (juce::Thread::Priority::low);
    }
    return changed;
}

std::int64_t PatchBrowser::userSavedRootMtime() const
{
    const juce::File dir (pathString (resolveUserSavedRoot()));
    return dir.exists() ? dir.getLastModificationTime().toMilliseconds() : 0;
}

std::int64_t PatchBrowser::userImportedRootMtime() const
{
    const juce::File dir (pathString (resolveUserImportedRoot()));
    return dir.exists() ? dir.getLastModificationTime().toMilliseconds() : 0;
}

// =============================================================================
// Factory patches
// =============================================================================

int PatchBrowser::numFactoryPatches() const noexcept
{
    return static_cast<int> (factoryPatches.size());
}

const Patch* PatchBrowser::factoryPatchByIndex (int n) const noexcept
{
    if (n < 0 || n >= static_cast<int> (factoryPatches.size()))
        return nullptr;
    return &factoryPatches[(std::size_t) n];
}

// =============================================================================
// Save / Import / Export
// =============================================================================

PatchBrowser::SaveResult
PatchBrowser::savePatchAsTfi (const Patch& patch, const juce::String& name)
{
    SaveResult result;

    // 04-patch-system.md: savePatch() writes into the user-saved root only.
    const auto savedFs = resolveUserSavedRoot();
    std::error_code ec;
    fs::create_directories (savedFs, ec);
    if (! fs::is_directory (savedFs, ec))
    {
        result.error = "cannot create user-saved patch directory";
        return result;
    }

    const auto safe = sanitiseFileName (name.isEmpty() ? juce::String ("patch") : name);
    const fs::path dest = savedFs / (safe.toStdString() + ".tfi");

    auto err = exportTFI (patch, dest);
    if (! err.empty())
    {
        result.error = std::move (err);
        return result;
    }

    // Refresh the user-saved root so the new patch shows up.
    if (auto* savedRoot = findFolderByPath (pathString (savedFs)))
    {
        savedRoot->scanned = false;
        scanImmediateChildren (*savedRoot);
    }

    indexBuilt.store (false, std::memory_order_release);
    if (isThreadRunning())
        notify();
    else
        startThread (juce::Thread::Priority::low);

    result.path = pathString (dest);
    return result;
}

std::string PatchBrowser::importPatchFile (const juce::String& sourcePath)
{
    const fs::path src { sourcePath.toRawUTF8() };
    std::error_code ec;
    if (! fs::is_regular_file (src, ec))
        return "import source is not a regular file: " + sourcePath.toStdString();
    if (! isPatchExtension (src))
        return "import source is not a supported patch file (" + buildPatchExtensionFilter() + ")";

    // 04-patch-system.md: importPatch() and drag-and-drop both land in the
    // user-imported root.
    const auto importedFs = resolveUserImportedRoot();
    fs::create_directories (importedFs, ec);
    const fs::path dest = importedFs / src.filename();
    fs::copy_file (src, dest, fs::copy_options::overwrite_existing, ec);
    if (ec)
        return "copy failed: " + ec.message();

    if (auto* importedRoot = findFolderByPath (pathString (importedFs)))
    {
        importedRoot->scanned = false;
        scanImmediateChildren (*importedRoot);
    }
    indexBuilt.store (false, std::memory_order_release);
    if (isThreadRunning()) notify();
    else                   startThread (juce::Thread::Priority::low);
    return {};
}

PatchBrowser::FolderImportResult
PatchBrowser::importPatchFolder (const juce::String& sourcePath)
{
    FolderImportResult result {};
    const fs::path root { sourcePath.toRawUTF8() };
    std::error_code ec;
    if (! fs::is_directory (root, ec))
    {
        result.errors.push_back ("source is not a directory: "
                                 + sourcePath.toStdString());
        return result;
    }

    const auto importedFs = resolveUserImportedRoot();
    fs::create_directories (importedFs, ec);

    // Recursive walk; collect patch files first so we can guarantee the index
    // rebuild + folder rescan run exactly once at the end.
    for (auto it = fs::recursive_directory_iterator (root, ec);
         it != fs::recursive_directory_iterator{};
         it.increment (ec))
    {
        if (ec) { ec.clear(); continue; }
        const auto& entry = *it;
        if (! entry.is_regular_file (ec)) { ec.clear(); continue; }
        if (! isPatchExtension (entry.path())) { ++result.skipped; continue; }

        const fs::path dest = importedFs / entry.path().filename();
        std::error_code copyEc;
        fs::copy_file (entry.path(), dest,
                       fs::copy_options::overwrite_existing, copyEc);
        if (copyEc)
            result.errors.push_back ("copy failed for "
                + entry.path().filename().string() + ": " + copyEc.message());
        else
            ++result.imported;
    }

    if (result.imported > 0)
    {
        if (auto* importedRoot = findFolderByPath (pathString (importedFs)))
        {
            importedRoot->scanned = false;
            scanImmediateChildren (*importedRoot);
        }
        indexBuilt.store (false, std::memory_order_release);
        if (isThreadRunning()) notify();
        else                   startThread (juce::Thread::Priority::low);
    }

    return result;
}

PatchBrowser::VgmImportSaveResult
PatchBrowser::saveExtractedPatches (const std::vector<Patch>& patches)
{
    VgmImportSaveResult result;
    if (patches.empty())
        return result;

    const auto importedFs = resolveUserImportedRoot();
    std::error_code ec;
    fs::create_directories (importedFs, ec);

    for (const auto& p : patches)
    {
        const juce::String safe = sanitiseFileName (
            juce::String (p.name).isEmpty() ? juce::String ("patch") : juce::String (p.name));
        const fs::path dest = importedFs / (safe.toStdString() + ".tfi");

        const auto err = exportTFI (p, dest);
        if (err.empty())
            ++result.saved;
        else
            result.errors.push_back (err);
    }

    // No folder-tree mutation here — the caller hops back to the message
    // thread and calls rescanWritableRoots() to refresh the tree and trigger
    // the search-index rebuild. Doing both here would race against the JS
    // patch-browser modal's tree reads.
    return result;
}

std::string PatchBrowser::deletePatchFile (const juce::String& absolutePath)
{
    const fs::path path { absolutePath.toRawUTF8() };
    std::error_code ec;
    if (! fs::is_regular_file (path, ec))
        return "delete target is not a regular file: " + absolutePath.toStdString();
    if (! isPatchExtension (path))
        return "delete target is not a supported patch file (" + buildPatchExtensionFilter() + ")";

    // Only allow deletes inside a writable root. We resolve once and compare
    // canonical paths so a path like `…/saved/../saved/foo.tfi` still matches.
    const auto canonTarget = fs::weakly_canonical (path, ec);
    bool inWritable = false;
    juce::String parentRootPath;
    for (const auto& r : rootList)
    {
        if (! r->writable || r->folder == nullptr) continue;
        const fs::path rootFs { r->folder->path.toRawUTF8() };
        const auto canonRoot = fs::weakly_canonical (rootFs, ec);
        // canonTarget begins with canonRoot? std::filesystem doesn't expose
        // starts_with directly until C++20 lexically_relative; compare via
        // proximate-style logic with a path iterator walk.
        auto a = canonRoot.begin();
        auto b = canonTarget.begin();
        bool prefix = true;
        for (; a != canonRoot.end(); ++a, ++b)
        {
            if (b == canonTarget.end() || *a != *b) { prefix = false; break; }
        }
        if (prefix)
        {
            inWritable = true;
            parentRootPath = r->folder->path;
            break;
        }
    }
    if (! inWritable)
        return "refusing to delete: path is not inside a writable root";

    fs::remove (path, ec);
    if (ec)
        return "delete failed: " + ec.message();

    // Refresh every scanned folder that might have held the deleted file. The
    // file's immediate parent is the obvious candidate; refresh the root too
    // because the immediate parent IS the root in the common case.
    if (auto* parent = findFolderByPath (pathString (path.parent_path())))
    {
        parent->scanned = false;
        scanImmediateChildren (*parent);
    }
    if (auto* root = findFolderByPath (parentRootPath))
    {
        root->scanned = false;
        scanImmediateChildren (*root);
    }

    indexBuilt.store (false, std::memory_order_release);
    if (isThreadRunning()) notify();
    else                   startThread (juce::Thread::Priority::low);
    return {};
}

std::string PatchBrowser::exportPatchToPath (const Patch&        patch,
                                             const juce::String& destinationPath)
{
    const fs::path dest { destinationPath.toRawUTF8() };
    const auto ext = dest.extension().string();
    if (ext == ".tfi" || ext == ".TFI") return exportTFI (patch, dest);
    if (ext == ".vgi" || ext == ".VGI") return exportVGI (patch, dest);
    return "unrecognised export extension: " + ext;
}

// =============================================================================
// JSON helpers (juce::var trees)
// =============================================================================

juce::var PatchBrowser::folderSummaryJson (const PatchFolder& f)
{
    auto* obj = new juce::DynamicObject();
    obj->setProperty ("path",       f.path);
    obj->setProperty ("name",       f.name);
    obj->setProperty ("scanned",    f.scanned);
    obj->setProperty ("patchCount", f.patchCount);
    return juce::var (obj);
}

juce::var PatchBrowser::folderFullJson (const PatchFolder& f)
{
    auto* obj = new juce::DynamicObject();
    obj->setProperty ("path",       f.path);
    obj->setProperty ("name",       f.name);
    obj->setProperty ("scanned",    f.scanned);
    obj->setProperty ("patchCount", f.patchCount);

    juce::Array<juce::var> subs;
    subs.ensureStorageAllocated ((int) f.subfolders.size());
    for (const auto& s : f.subfolders)
        subs.add (folderSummaryJson (*s));
    obj->setProperty ("subfolders", subs);

    juce::Array<juce::var> patches;
    patches.ensureStorageAllocated ((int) f.patches.size());
    for (const auto& p : f.patches)
    {
        auto* po = new juce::DynamicObject();
        po->setProperty ("name", p.name);
        po->setProperty ("path", p.path);
        patches.add (juce::var (po));
    }
    obj->setProperty ("patches", patches);

    return juce::var (obj);
}

juce::var PatchBrowser::rootsAsJson() const
{
    juce::Array<juce::var> arr;
    arr.ensureStorageAllocated ((int) rootList.size());
    for (const auto& r : rootList)
    {
        auto* obj = new juce::DynamicObject();
        const char* kindStr = r->kind == PatchRootKind::Factory      ? "factory"
                            : r->kind == PatchRootKind::UserSaved    ? "user-saved"
                            : r->kind == PatchRootKind::UserImported ? "user-imported"
                                                                     : "custom";
        obj->setProperty ("kind",        kindStr);
        obj->setProperty ("id",          r->id);
        obj->setProperty ("displayName", r->displayName);
        obj->setProperty ("writable",    r->writable);
        obj->setProperty ("path",        r->folder ? r->folder->path : juce::String());
        obj->setProperty ("scanned",     r->folder && r->folder->scanned);
        obj->setProperty ("patchCount",  r->folder ? r->folder->patchCount : -1);

        juce::Array<juce::var> subs;
        if (r->folder)
        {
            subs.ensureStorageAllocated ((int) r->folder->subfolders.size());
            for (const auto& s : r->folder->subfolders)
                subs.add (folderSummaryJson (*s));
        }
        obj->setProperty ("subfolders", subs);
        arr.add (juce::var (obj));
    }
    return juce::var (arr);
}

juce::var PatchBrowser::folderAsJson (const juce::String& folderPath)
{
    if (auto* f = findFolderByPath (folderPath))
    {
        if (! f->scanned)
            scanImmediateChildren (*f);
        return folderFullJson (*f);
    }
    return {};
}

juce::var PatchBrowser::searchAsJson (const juce::String& query) const
{
    juce::Array<juce::var> arr;
    for (const auto& hit : search (query))
    {
        auto* obj = new juce::DynamicObject();
        obj->setProperty ("name",       hit.name);
        obj->setProperty ("path",       hit.path);
        obj->setProperty ("rootId",     hit.rootId);
        obj->setProperty ("folderPath", hit.folderPath);
        arr.add (juce::var (obj));
    }
    return juce::var (arr);
}

// =============================================================================
// Path resolution
// =============================================================================

fs::path PatchBrowser::resolveUserRootBase()
{
    const auto base = juce::File::getSpecialLocation (
                          juce::File::SpecialLocationType::userApplicationDataDirectory);
    const auto user = base.getChildFile ("GenVst").getChildFile ("patches");
    return fs::path (user.getFullPathName().toRawUTF8());
}

fs::path PatchBrowser::resolveUserSavedRoot()
{
    return resolveUserRootBase() / "saved";
}

fs::path PatchBrowser::resolveUserImportedRoot()
{
    return resolveUserRootBase() / "imported";
}

juce::String PatchBrowser::rootIdFor (PatchRootKind kind, const juce::String& absolutePath)
{
    if (kind == PatchRootKind::Factory)      return "factory";
    if (kind == PatchRootKind::UserSaved)    return "user-saved";
    if (kind == PatchRootKind::UserImported) return "user-imported";

    // Custom roots: a short, stable id derived from the path's hash. Stable
    // across sessions for the same path so a state restore (Task 16) can
    // match an old root by id. juce::String::hashCode64 -> 16-hex-digit suffix.
    const auto digest = juce::String::toHexString ((juce::int64) absolutePath.hashCode64());
    return "custom-" + digest;
}

juce::String PatchBrowser::sanitiseFileName (const juce::String& name)
{
    static const juce::String forbidden { "<>:\"/\\|?*" };
    juce::String out;
    out.preallocateBytes ((size_t) name.length() * 2);
    for (auto c : name)
    {
        if (forbidden.containsChar (c) || c < 32)
            out += '_';
        else
            out += c;
    }
    // Trim trailing whitespace / dots (Windows hates them).
    while (out.endsWithChar (' ') || out.endsWithChar ('.'))
        out = out.dropLastCharacters (1);
    if (out.isEmpty())
        out = "patch";
    return out;
}

} // namespace genvst
