#include <gtest/gtest.h>
#include "../include/ipc_layout.hpp"
#include <cstddef>

TEST(MemoryLayoutTest, CacheLineAlignmentAndSizes) {
    EXPECT_EQ(CACHE_LINE_SIZE, 64);
    EXPECT_EQ(sizeof(IPCHeader), 192);
    EXPECT_EQ(sizeof(Slot), 64);

    // Verify critical cache line boundaries
    EXPECT_EQ(offsetof(IPCHeader, write_index), 0);
    EXPECT_EQ(offsetof(IPCHeader, read_index), 64);
    EXPECT_EQ(offsetof(IPCHeader, futex_word), 128);
}
