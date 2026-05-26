// Tests for the Task 09 patch-browser backend: roots, lazy folder scan,
// background search index, the lock-free delivery queue, and the load-failure
// path. The audio-thread atomic-stores driven by drainAudioThreadQueue are
// covered by pluginval (no allocation/locks), so this file focuses on the
// message-thread surface.

#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

#include <gtest/gtest.h>

#include "PatchBrowser.h"
#include "PatchSystem.h"

namespace fs = std::filesystem;

#ifndef GENVST_FACTORY_PATCHES_DIR
 #error "GENVST_FACTORY_PATCHES_DIR must be defined by the test build (see tests/CMakeLists.txt)"
#endif

namespace
{
    fs::path factoryDir() { return fs::path { GENVST_FACTORY_PATCHES_DIR }; }

    // The on-repo extra/ test set sits beside extern/patches/ — used as the
    // 30k-file stress fixture for the lazy-scan / search-index tests. Some
    // checkouts may not have populated it (it is gitignored), so tests guard
    // with HasFiles().
    fs::path extraDir()
    {
        return fs::path { GENVST_FACTORY_PATCHES_DIR } / ".." / ".." / "extra";
    }

    bool extraExistsWithChildren()
    {
        std::error_code ec;
        if (! fs::is_directory (extraDir(), ec))
            return false;
        for ([[maybe_unused]] const auto& e : fs::directory_iterator (extraDir(), ec))
            return true;
        return false;
    }
}

// -- Factory root resolution: 39 patches ------------------------------------
TEST (PatchBrowser, FactoryRootLoadsFactoryPatches)
{
    genvst::PatchBrowser browser;
    browser.initialize (factoryDir());

    // The repo ships ~39 .tfi files at the top of extern/patches/; the
    // browser should expose them all as the factory bank for Program Change
    // and as the factory root's top-level entries.
    EXPECT_GT (browser.numFactoryPatches(), 30);

    const auto& rootsVec = browser.roots();
    // factory + user-saved + user-imported (Task 14: the writable root was
    // split into saved/imported subroots).
    ASSERT_GE (rootsVec.size(), 3u);

    const auto& factoryFolder = *rootsVec[0]->folder;
    EXPECT_TRUE (factoryFolder.scanned);
    EXPECT_EQ ((int) factoryFolder.patches.size(), browser.numFactoryPatches());

    // One subfolder: extern/patches/sq/ (the SQ factory presets shipped
    // alongside the flat top-level Furnace tfilib). Task 09 added this.
    EXPECT_EQ (factoryFolder.subfolders.size(), 1u);
    EXPECT_EQ (factoryFolder.subfolders.front()->name, juce::String ("sq"));

    EXPECT_EQ (rootsVec[0]->id, juce::String ("factory"));
    EXPECT_EQ (rootsVec[1]->id, juce::String ("user-saved"));
    EXPECT_EQ (rootsVec[2]->id, juce::String ("user-imported"));
}

// -- Task 14: rootsAsJson serialises the two new writable kinds -------------
TEST (PatchBrowser, RootsAsJsonEmitsSavedAndImportedKinds)
{
    genvst::PatchBrowser browser;
    browser.initialize (factoryDir());

    const auto v = browser.rootsAsJson();
    ASSERT_TRUE (v.isArray());

    bool sawSaved = false, sawImported = false, sawLegacyUser = false;
    for (int i = 0; i < v.size(); ++i)
    {
        const auto kind = v[i].getProperty ("kind", juce::var()).toString();
        if (kind == "user-saved")    sawSaved    = true;
        if (kind == "user-imported") sawImported = true;
        if (kind == "user")          sawLegacyUser = true;
    }
    EXPECT_TRUE (sawSaved);
    EXPECT_TRUE (sawImported);
    EXPECT_FALSE (sawLegacyUser) << "old \"user\" kind must be retired";
}

// -- savePatch lands in saved/, importPatch in imported/ ---------------------
TEST (PatchBrowser, SaveAndImportLandInDistinctSubroots)
{
    genvst::PatchBrowser browser;
    browser.initialize (factoryDir());

    // Save: synthesises a tiny patch and writes it via savePatchAsTfi. The
    // file should land under <userAppData>/GenVst/patches/saved/ and inside
    // the user-saved root's folder list.
    Patch p {};
    p.alg = 4; p.fb = 2;
    const auto saved = browser.savePatchAsTfi (p, "task14_save_smoke");
    ASSERT_TRUE (saved.error.empty()) << saved.error;
    ASSERT_FALSE (saved.path.isEmpty());

    // The path must contain a "saved" segment.
    EXPECT_NE (saved.path.indexOf ("saved"), -1)
        << "savePatch wrote outside the saved/ subroot: " << saved.path;

    // Import: write a temp TFI we'll re-import. The file should land under
    // .../imported/ and inside the user-imported root's folder list.
    const fs::path src = fs::temp_directory_path() / "task14_import_smoke.tfi";
    {
        std::ofstream out (src, std::ios::binary | std::ios::trunc);
        const std::vector<char> zeros (kTfiFileSize, 0);
        out.write (zeros.data(), (std::streamsize) zeros.size());
    }
    const auto importErr = browser.importPatchFile (juce::String (src.string()));
    fs::remove (src);
    ASSERT_TRUE (importErr.empty()) << importErr;

    bool sawSavedFile = false, sawImportedFile = false;
    for (const auto& r : browser.roots())
    {
        if (r->kind == genvst::PatchRootKind::UserSaved)
            for (const auto& patch : r->folder->patches)
                if (patch.name == "task14_save_smoke") sawSavedFile = true;
        if (r->kind == genvst::PatchRootKind::UserImported)
            for (const auto& patch : r->folder->patches)
                if (patch.name == "task14_import_smoke") sawImportedFile = true;
    }
    EXPECT_TRUE (sawSavedFile);
    EXPECT_TRUE (sawImportedFile);

    // Cleanup: delete what the test wrote so reruns are idempotent.
    browser.deletePatchFile (saved.path);
    for (const auto& r : browser.roots())
    {
        if (r->kind != genvst::PatchRootKind::UserImported) continue;
        for (const auto& patch : r->folder->patches)
            if (patch.name == "task14_import_smoke")
                browser.deletePatchFile (patch.path);
    }
}

// -- deletePatchFile only works inside writable roots -----------------------
TEST (PatchBrowser, DeletePatchRefusesFactoryAndSucceedsOnSaved)
{
    genvst::PatchBrowser browser;
    browser.initialize (factoryDir());

    // Refuses any factory file. Pick the first factory entry and try.
    const auto& factoryFolder = *browser.roots()[0]->folder;
    ASSERT_FALSE (factoryFolder.patches.empty());
    const auto factoryPath = factoryFolder.patches[0].path;

    const auto factoryErr = browser.deletePatchFile (factoryPath);
    EXPECT_FALSE (factoryErr.empty()) << "Delete must reject factory paths";
    // The file is still there.
    EXPECT_TRUE (fs::is_regular_file (fs::path (factoryPath.toRawUTF8())));

    // Saves a temporary patch and deletes it — the file should be gone.
    Patch p {};
    const auto saved = browser.savePatchAsTfi (p, "task14_delete_smoke");
    ASSERT_TRUE (saved.error.empty()) << saved.error;
    ASSERT_TRUE (fs::is_regular_file (fs::path (saved.path.toRawUTF8())));

    const auto delErr = browser.deletePatchFile (saved.path);
    EXPECT_TRUE (delErr.empty()) << delErr;
    EXPECT_FALSE (fs::is_regular_file (fs::path (saved.path.toRawUTF8())));

    // The saved root should no longer list it.
    for (const auto& r : browser.roots())
    {
        if (r->kind != genvst::PatchRootKind::UserSaved) continue;
        for (const auto& patch : r->folder->patches)
            EXPECT_NE (patch.name, juce::String ("task14_delete_smoke"));
    }
}

// -- Lazy scan on a custom root: top-level only, deep folders unscanned ----
TEST (PatchBrowser, CustomRootScanIsLazy)
{
    if (! extraExistsWithChildren())
        GTEST_SKIP() << "extra/ test set not present; lazy-scan stress test skipped";

    genvst::PatchBrowser browser;
    browser.initialize (factoryDir());

    // Adding the custom root should be fast — only the immediate children of
    // extra/ are read, not the 30k files under them. A few seconds of slack
    // covers slow CI disks; a recursive walk would take far longer.
    const auto t0 = std::chrono::steady_clock::now();
    const auto id = browser.addCustomRoot (juce::String (extraDir().string()));
    const auto elapsed = std::chrono::steady_clock::now() - t0;
    const auto secs = std::chrono::duration_cast<std::chrono::milliseconds> (elapsed).count() / 1000.0;

    ASSERT_FALSE (id.isEmpty());
    EXPECT_LT (secs, 5.0) << "addCustomRoot took " << secs << "s — appears to be eagerly scanning";

    // Find the custom root in the list.
    const genvst::PatchRoot* custom = nullptr;
    for (const auto& r : browser.roots())
        if (r->id == id) { custom = r.get(); break; }
    ASSERT_NE (custom, nullptr);

    // Each subfolder of extra/ (01, 02, ...) is recognised but unscanned.
    ASSERT_GT (custom->folder->subfolders.size(), 0u);
    for (const auto& sub : custom->folder->subfolders)
    {
        EXPECT_FALSE (sub->scanned) << "subfolder " << sub->name << " scanned at startup";
        EXPECT_EQ (sub->patchCount, -1)  << "subfolder " << sub->name << " has a patch count before expansion";
    }

    // Expanding ONE subfolder fills in its count but leaves the rest alone.
    auto& deep = *custom->folder->subfolders[0];
    browser.expandFolder (deep);
    EXPECT_TRUE (deep.scanned);
    EXPECT_GE (deep.patchCount, 0);

    if (custom->folder->subfolders.size() > 1)
        EXPECT_FALSE (custom->folder->subfolders[1]->scanned)
            << "expanding one subfolder eagerly scanned its sibling";
}

// -- Background search index returns factory hits ---------------------------
TEST (PatchBrowser, SearchIndexHitsFactoryNames)
{
    genvst::PatchBrowser browser;
    browser.initialize (factoryDir());

    // The indexer runs on a background juce::Thread; poll for up to 5s. With
    // the factory dir of ~39 files, indexing completes essentially instantly.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds (5);
    while (! browser.isSearchIndexReady() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for (std::chrono::milliseconds (20));

    ASSERT_TRUE (browser.isSearchIndexReady()) << "background indexer never finished";

    const auto hits = browser.search ("organ", 50);
    EXPECT_FALSE (hits.empty()) << "no hits for 'organ' — index missed the factory bank";

    bool foundFactory = false;
    for (const auto& h : hits)
        if (h.rootId == "factory" && h.name.equalsIgnoreCase ("organ"))
            { foundFactory = true; break; }
    EXPECT_TRUE (foundFactory);
}

// -- Successful load pushes into the SPSC queue -----------------------------
TEST (PatchBrowser, LoadIntoPartPushesValidPatchToQueue)
{
    genvst::PatchBrowser browser;
    browser.initialize (factoryDir());

    const auto organ = (factoryDir() / "organ.tfi").string();
    const auto err = browser.loadIntoPart (1, juce::String (organ));
    EXPECT_TRUE (err.empty()) << err;

    EXPECT_EQ (browser.activePatchPath (1), juce::String (organ));

    // Drain the queue to confirm a (part, Patch) showed up.
    int seenPart    = -1;
    juce::String seenName;
    browser.drainAudioThreadQueue ([&] (int part, const Patch& p)
    {
        seenPart = part;
        seenName = juce::String (p.name);
    });
    EXPECT_EQ (seenPart, 1);
    EXPECT_EQ (seenName, "organ");
}

// -- A failed load returns an error and pushes nothing to the queue --------
TEST (PatchBrowser, FailedLoadDoesNotReachQueue)
{
    genvst::PatchBrowser browser;
    browser.initialize (factoryDir());

    // Write a wrong-size temp file so the TFI loader rejects it.
    const fs::path tmp = fs::temp_directory_path() / "genvst_browser_bad.tfi";
    {
        std::ofstream out (tmp, std::ios::binary | std::ios::trunc);
        const std::vector<char> junk (10, 0);   // 10 bytes != kTfiFileSize (42)
        out.write (junk.data(), (std::streamsize) junk.size());
    }

    const auto err = browser.loadIntoPart (2, juce::String (tmp.string()));
    fs::remove (tmp);

    EXPECT_FALSE (err.empty());   // error must surface to the caller
    EXPECT_TRUE (browser.activePatchPath (2).isEmpty());

    bool sawDelivery = false;
    browser.drainAudioThreadQueue ([&] (int, const Patch&) { sawDelivery = true; });
    EXPECT_FALSE (sawDelivery) << "queue saw a failed load — must be filtered on the message thread";
}

// -- Custom root unregister never deletes from disk -------------------------
TEST (PatchBrowser, RemoveCustomRootLeavesFilesIntact)
{
    if (! extraExistsWithChildren())
        GTEST_SKIP() << "extra/ test set not present";

    genvst::PatchBrowser browser;
    browser.initialize (factoryDir());

    const auto id = browser.addCustomRoot (juce::String (extraDir().string()));
    ASSERT_FALSE (id.isEmpty());

    ASSERT_TRUE (browser.removeCustomRoot (id));

    // The directory still exists.
    EXPECT_TRUE (fs::is_directory (extraDir()));

    // Removing the same id twice is a no-op (returns false).
    EXPECT_FALSE (browser.removeCustomRoot (id));
}

// -- Program Change source: factoryPatchByIndex hands back numeric data -----
TEST (PatchBrowser, FactoryPatchByIndexBoundsCheck)
{
    genvst::PatchBrowser browser;
    browser.initialize (factoryDir());

    EXPECT_EQ (browser.factoryPatchByIndex (-1), nullptr);
    EXPECT_EQ (browser.factoryPatchByIndex (browser.numFactoryPatches()), nullptr);

    if (browser.numFactoryPatches() > 0)
    {
        const auto* p = browser.factoryPatchByIndex (0);
        ASSERT_NE (p, nullptr);
        EXPECT_FALSE (p->name.empty());
    }
}

// -- Factory `sq/` subfolder is eagerly scanned at startup ------------------
// initialize() recursively scans the factory root's immediate subfolders so
// the SQ chip filter in the preset browser (driven by listAllPresetsAsJson,
// which only recurses into already-scanned subfolders) sees the 12 .psg
// presets without the user having to manually expand the folder. Regression
// test for the post-mvp2 "I didn't see more than 1 preset for SQ" report.
TEST (PatchBrowser, FactorySqSubfolderEagerlyScannedAtStartup)
{
    genvst::PatchBrowser browser;
    browser.initialize (factoryDir());

    const auto& rootsVec = browser.roots();
    ASSERT_FALSE (rootsVec.empty());
    const auto& factoryFolder = *rootsVec[0]->folder;

    // Find the sq/ subfolder.
    const genvst::PatchFolder* sq = nullptr;
    for (const auto& sub : factoryFolder.subfolders)
        if (sub != nullptr && sub->name == juce::String ("sq"))
        {
            sq = sub.get();
            break;
        }

    ASSERT_NE (sq, nullptr) << "factory/sq subfolder missing — did extern/patches/sq/ get committed?";
    EXPECT_TRUE (sq->scanned) << "factory/sq must be scanned at startup so the SQ chip filter sees its presets";

    // Every patch in sq/ should be tagged SQ (extension-derived; .psg → SQ
    // per PatchSystem::tagFromExtension).
    int sqCount = 0;
    for (const auto& entry : sq->patches)
    {
        ++sqCount;
        EXPECT_EQ (entry.tag, Tag::SQ)
            << "factory/sq/" << entry.name.toStdString()
            << " did not tag as SQ — extension dispatch broken?";
    }
    EXPECT_GE (sqCount, 12)
        << "expected at least 12 factory .psg presets (Task 10 deliverable); found " << sqCount;
}
