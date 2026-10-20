#include <gtest/gtest.h>
#include "../include/shm_region.hpp"
#include <sys/mman.h>

class ShmRegionTest : public ::testing::Test {
protected:
    const std::string test_name{"/test_shm_raii"};

    void SetUp() override {
        shm_unlink(test_name.c_str());
    }

    void TearDown() override {
        shm_unlink(test_name.c_str());
    }
};

// 1. Creation and automatic mapping
TEST_F(ShmRegionTest, CreateAndMapSuccessfully) {
    const size_t size = 4096;
    {
        auto shm = ipc::ShmRegion::create(test_name, size);
        EXPECT_NE(shm.get(), nullptr);
        EXPECT_NE(shm.get(), MAP_FAILED);
        EXPECT_EQ(shm.size(), size);
        EXPECT_EQ(shm.name(), test_name);

        // Write a byte to prove mapped memory is writable
        uint8_t* ptr = static_cast<uint8_t*>(shm.get());
        ptr[0] = 0x42;
        EXPECT_EQ(ptr[0], 0x42);
    }
    // After exiting scope, destructor must have called shm_unlink!
    // Trying to open must now fail with ENOENT
    int fd = shm_open(test_name.c_str(), O_RDWR, 0666);
    EXPECT_EQ(fd, -1);
    EXPECT_EQ(errno, ENOENT);
}

// 2. Open fails if SHM does not exist
TEST_F(ShmRegionTest, OpenNonExistentThrows) {
    EXPECT_THROW({
        auto shm = ipc::ShmRegion::open("/non_existent_shm_test_123", 4096);
    }, std::system_error);
}

// 3. Move semantics transfer ownership
TEST_F(ShmRegionTest, MoveConstructorTransfersOwnership) {
    const size_t size = 4096;
    auto shm1 = ipc::ShmRegion::create(test_name, size);
    void* original_ptr = shm1.get();

    // Move shm1 into shm2
    auto shm2 = std::move(shm1);

    // shm2 must own the original pointer
    EXPECT_EQ(shm2.get(), original_ptr);

    // shm1 must be safely neutralized
    EXPECT_EQ(shm1.get(), MAP_FAILED);
}
