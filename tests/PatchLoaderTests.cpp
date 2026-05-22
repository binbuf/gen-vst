#include <filesystem>
#include <fstream>
#include <vector>

#include <gtest/gtest.h>

#include "PatchSystem.h"

#ifndef GENVST_FACTORY_PATCHES_DIR
 #error "GENVST_FACTORY_PATCHES_DIR must be defined by the test build (see tests/CMakeLists.txt)"
#endif

namespace fs = std::filesystem;

namespace
{
    fs::path factoryDir()
    {
        return fs::path (GENVST_FACTORY_PATCHES_DIR);
    }

    // Assert every Patch field sits inside its valid hardware range. loadTFI
    // clamps on load, so this verifies the clamp held.
    void expectInHardwareRange (const Patch& p)
    {
        EXPECT_LE (static_cast<int> (p.alg), 7);
        EXPECT_LE (static_cast<int> (p.fb),  7);
        EXPECT_LE (static_cast<int> (p.lr),  3);
        EXPECT_LE (static_cast<int> (p.ams), 3);
        EXPECT_LE (static_cast<int> (p.pms), 7);

        for (int op = 0; op < 4; ++op)
        {
            SCOPED_TRACE (op);
            EXPECT_LE (static_cast<int> (p.mul[op]),  15);
            EXPECT_LE (static_cast<int> (p.dt[op]),    6);
            EXPECT_LE (static_cast<int> (p.tl[op]),  127);
            EXPECT_LE (static_cast<int> (p.ks[op]),    3);
            EXPECT_LE (static_cast<int> (p.ar[op]),   31);
            EXPECT_LE (static_cast<int> (p.dr[op]),   31);
            EXPECT_LE (static_cast<int> (p.sr[op]),   31);
            EXPECT_LE (static_cast<int> (p.rr[op]),   15);
            EXPECT_LE (static_cast<int> (p.sl[op]),   15);
            EXPECT_LE (static_cast<int> (p.amon[op]),  1);

            const int ssg = static_cast<int> (p.ssg[op]);
            EXPECT_TRUE (ssg == 0 || (ssg >= 8 && ssg <= 15)) << "ssg=" << ssg;
        }
    }
}

// Every factory .tfi file loads successfully.
TEST (PatchLoader, AllFactoryFilesLoad)
{
    ASSERT_TRUE (fs::is_directory (factoryDir())) << factoryDir();

    int count = 0;
    for (const auto& entry : fs::directory_iterator (factoryDir()))
    {
        if (! entry.is_regular_file() || entry.path().extension() != ".tfi")
            continue;

        ++count;
        SCOPED_TRACE (entry.path().filename().string());
        const PatchLoadResult result = loadTFI (entry.path());
        EXPECT_TRUE (result.patch.has_value());
        EXPECT_TRUE (result.error.empty());
    }

    EXPECT_GT (count, 0) << "no .tfi files found in " << factoryDir();
}

// Loaded values are clamped within their hardware ranges.
TEST (PatchLoader, LoadedValuesAreInHardwareRange)
{
    ASSERT_TRUE (fs::is_directory (factoryDir())) << factoryDir();

    for (const auto& entry : fs::directory_iterator (factoryDir()))
    {
        if (! entry.is_regular_file() || entry.path().extension() != ".tfi")
            continue;

        SCOPED_TRACE (entry.path().filename().string());
        const PatchLoadResult result = loadTFI (entry.path());
        ASSERT_TRUE (result.patch.has_value());
        expectInHardwareRange (*result.patch);
    }
}

// The patch name is taken from the file's stem.
TEST (PatchLoader, NameComesFromFilename)
{
    const PatchLoadResult result = loadTFI (factoryDir() / "organ.tfi");
    ASSERT_TRUE (result.patch.has_value());
    EXPECT_EQ (result.patch->name, "organ");
}

// A wrong-size file is rejected with an error and no patch.
TEST (PatchLoader, WrongSizeFileIsRejected)
{
    const fs::path tmp = fs::temp_directory_path() / "genvst_wrongsize.tfi";
    {
        std::ofstream out (tmp, std::ios::binary);
        ASSERT_TRUE (out.is_open());
        const std::vector<char> junk (10, 0);   // 10 bytes, not 42
        out.write (junk.data(), static_cast<std::streamsize> (junk.size()));
    }

    const PatchLoadResult result = loadTFI (tmp);
    fs::remove (tmp);

    EXPECT_FALSE (result.patch.has_value());
    EXPECT_FALSE (result.error.empty());
}

// A missing file is rejected with an error and no patch.
TEST (PatchLoader, MissingFileIsRejected)
{
    const PatchLoadResult result = loadTFI (factoryDir() / "does_not_exist.tfi");
    EXPECT_FALSE (result.patch.has_value());
    EXPECT_FALSE (result.error.empty());
}
