#ifndef SHM_REGION_HPP
#define SHM_REGION_HPP

#include <string>
#include <system_error>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <cerrno>
#include <utility>

namespace ipc {

/**
 * @brief RAII wrapper for POSIX Shared Memory (/dev/shm).
 * 
 * Guarantees:
 * 1. Automatic resource acquisition (shm_open, ftruncate, mmap).
 * 2. Automatic cleanup on destruction (munmap, close, shm_unlink).
 * 3. Exception safety: If any OS call fails, previously acquired resources are freed.
 * 4. Move-only semantics: Prevents duplicate ownership and double-free crashes.
 */
class ShmRegion {
public:
    // =========================================================================
    // Factory Methods (Named constructors for readability)
    // =========================================================================

    /**
     * @brief Create a brand-new shared memory segment (used by the Producer).
     * Creates the file in /dev/shm, resizes it, and maps it PROT_READ | PROT_WRITE.
     */
    static ShmRegion create(const std::string& name, size_t size) {
        return ShmRegion(name, size, true);
    }

    /**
     * @brief Attach to an existing shared memory segment (used by the Consumer).
     * Opens the existing file in /dev/shm and maps it into process virtual memory.
     */
    static ShmRegion open(const std::string& name, size_t size) {
        return ShmRegion(name, size, false);
    }

    // =========================================================================
    // Destructor (RAII Cleanup)
    // =========================================================================

    /**
     * @brief Automatically called when the ShmRegion object goes out of scope.
     * Unmaps virtual memory, closes file descriptors, and unlinks if creator.
     */
    ~ShmRegion() {
        // 1. Unmap virtual memory address space
        if (ptr_ != MAP_FAILED && ptr_ != nullptr) {
            munmap(ptr_, size_);
            ptr_ = MAP_FAILED;
        }

        // 2. Close file descriptor
        if (fd_ != -1) {
            close(fd_);
            fd_ = -1;
        }

        // 3. If we created the shared memory, remove it from the OS (/dev/shm)
        if (is_creator_ && !name_.empty()) {
            shm_unlink(name_.c_str());
        }
    }

    // =========================================================================
    // Ownership Semantics (Rule of 5: Move-only, Non-copyable)
    // =========================================================================

    // Delete Copy Constructor & Copy Assignment
    // Reason: Two objects must NEVER own the same file descriptor and memory pointer,
    // otherwise both destructors would call munmap() -> double free crash!
    ShmRegion(const ShmRegion&) = delete;
    ShmRegion& operator=(const ShmRegion&) = delete;

    // Move Constructor: Transfer ownership of OS handles to a new object
    ShmRegion(ShmRegion&& other) noexcept
        : name_(std::move(other.name_)),
          size_(other.size_),
          fd_(other.fd_),
          ptr_(other.ptr_),
          is_creator_(other.is_creator_) {
        // Invalidate other so its destructor does not clean up our resources
        other.ptr_ = MAP_FAILED;
        other.fd_ = -1;
        other.is_creator_ = false;
        other.size_ = 0;
    }

    // Move Assignment Operator: Transfer ownership across existing variables
    ShmRegion& operator=(ShmRegion&& other) noexcept {
        if (this != &other) {
            // First, cleanup our own existing resources
            if (ptr_ != MAP_FAILED && ptr_ != nullptr) munmap(ptr_, size_);
            if (fd_ != -1) close(fd_);
            if (is_creator_ && !name_.empty()) shm_unlink(name_.c_str());

            // Then steal resources from 'other'
            name_ = std::move(other.name_);
            size_ = other.size_;
            fd_ = other.fd_;
            ptr_ = other.ptr_;
            is_creator_ = other.is_creator_;

            // Invalidate 'other'
            other.ptr_ = MAP_FAILED;
            other.fd_ = -1;
            other.is_creator_ = false;
            other.size_ = 0;
        }
        return *this;
    }

    // =========================================================================
    // Accessors
    // =========================================================================

    /// Returns the raw pointer to the mapped shared memory block
    void*  get() const noexcept { return ptr_; }

    /// Returns the mapped region size in bytes
    size_t size() const noexcept { return size_; }

    /// Returns the name of the shared memory region
    const std::string& name() const noexcept { return name_; }

private:
    // =========================================================================
    // Single Unified Private Section: Internal Variables & Constructor
    // =========================================================================

    std::string name_;              // Name of the SHM block (e.g., "/shm_ipc_test")
    size_t      size_{0};           // Size of the mapped region in bytes
    int         fd_{-1};            // POSIX file descriptor returned by shm_open
    void*       ptr_{MAP_FAILED};   // Virtual address pointer returned by mmap
    bool        is_creator_{false}; // True if this process created the SHM segment

    // Private constructor (enforces using ShmRegion::create() or ShmRegion::open())
    ShmRegion(const std::string& name, size_t size, bool create_new)
        : name_(name), size_(size), is_creator_(create_new) {

        int flags = O_RDWR;
        if (create_new) {
            flags |= O_CREAT; // Create file in /dev/shm if it doesn't exist
        }

        // 1. shm_open: Open or create the POSIX shared memory file
        fd_ = shm_open(name_.c_str(), flags, 0666);
        if (fd_ == -1) {
            throw std::system_error(errno, std::generic_category(),
                                    "shm_open failed for " + name_);
        }

        // 2. ftruncate: Set the file size (only creator resizes it)
        if (create_new) {
            if (ftruncate(fd_, size_) == -1) {
                int saved_errno = errno;
                close(fd_);
                shm_unlink(name_.c_str());
                throw std::system_error(saved_errno, std::generic_category(),
                                        "ftruncate failed for " + name_);
            }
        }

        // 3. mmap: Map the file into the process virtual address space
        ptr_ = mmap(nullptr, size_, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0);
        if (ptr_ == MAP_FAILED) {
            int saved_errno = errno;
            close(fd_);
            if (create_new) {
                shm_unlink(name_.c_str());
            }
            throw std::system_error(saved_errno, std::generic_category(),
                                        "mmap failed for " + name_);
        }
    }
};

} // namespace ipc

#endif // SHM_REGION_HPP
