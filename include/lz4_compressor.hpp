// Copyright (c) 2026 zFTL Project. All Rights Reserved.
// In-Line Compressed Flash Translation Layer for QLC NAND Flash.
#pragma once

#include <cstdint>
#include <cstddef>
#include <array>
#include <string_view>
#include <span>

namespace zftl {

// Standard NVMe Sector / Flash Page Granularity
inline constexpr size_t FLASH_PAGE_SIZE = 4096; // 4 KB

// Granular physical slot allocation on NAND page (1KB sub-page packing)
enum class SlotAllocation : uint8_t {
    SPARSE_ZERO = 0, // 0 bytes on flash (virtual pointer)
    SLOT_1KB    = 1, // <= 1024 bytes (packs up to 4 logical blocks per physical page)
    SLOT_2KB    = 2, // <= 2048 bytes (packs 2 logical blocks per physical page)
    SLOT_3KB    = 3, // <= 3072 bytes
    FULL_4KB    = 4  // Incompressible or raw 4096 bytes
};

constexpr std::string_view SlotToString(SlotAllocation slot) noexcept {
    switch (slot) {
        case SlotAllocation::SPARSE_ZERO: return "Sparse-Zero (0B)";
        case SlotAllocation::SLOT_1KB:    return "Quarter-Page (1KB)";
        case SlotAllocation::SLOT_2KB:    return "Half-Page (2KB)";
        case SlotAllocation::SLOT_3KB:    return "Three-Quarter (3KB)";
        case SlotAllocation::FULL_4KB:    return "Full-Page (4KB)";
        default:                          return "Unknown";
    }
}

// Result of a 4 KB block compression operation
struct CompressedBlock {
    bool is_zero_block{false};
    bool is_compressed{false};
    uint16_t compressed_size{static_cast<uint16_t>(FLASH_PAGE_SIZE)};
    SlotAllocation slot{SlotAllocation::FULL_4KB};
    std::array<uint8_t, FLASH_PAGE_SIZE> data{};
};

class LZ4Compressor {
public:
    // Compresses a 4,096-byte host block
    static CompressedBlock Compress(const void* src_4kb) noexcept;

    // Decompresses a CompressedBlock back into a 4,096-byte host buffer
    // Returns true on success, false on corruption
    static bool Decompress(const CompressedBlock& comp, void* dest_4kb) noexcept;

    // Computes the physical slot allocation required for a given byte size
    static SlotAllocation QuantizeSlot(uint16_t bytes, bool is_zero) noexcept;
};

} // namespace zftl
