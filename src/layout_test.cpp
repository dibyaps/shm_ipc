#include "../include/ipc_layout.hpp"
#include <iostream>
#include <cstddef>

int main() {
    std::cout << "--- IPC Memory Layout Verification ---\n";
    std::cout << "Cache Line Size:   " << CACHE_LINE_SIZE << " bytes\n";
    std::cout << "IPCHeader Size:    " << sizeof(IPCHeader) << " bytes\n";
    std::cout << "Slot Size:         " << sizeof(Slot) << " bytes\n\n";

    std::cout << "Offsets from start of IPCHeader:\n";
    std::cout << "write_index:       " << offsetof(IPCHeader, write_index) << " (Expected: 0)\n";
    std::cout << "read_index:        " << offsetof(IPCHeader, read_index) << " (Expected: 64)\n";
    std::cout << "futex_word:        " << offsetof(IPCHeader, futex_word) << " (Expected: 128)\n";

    return 0;
}