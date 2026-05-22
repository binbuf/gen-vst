#include <gtest/gtest.h>

// Proves the GoogleTest + CTest wiring is end-to-end. Real tests arrive later.
TEST (Smoke, TestWiringIsAlive)
{
    EXPECT_TRUE (true);
}
