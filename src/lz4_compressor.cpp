// Copyright (c) 2026 zFTL Project. All Rights Reserved.
// In-Line Compressed Flash Translation Layer for QLC NAND Flash.
#include "lz4_compressor.hpp"
#include "lz4.h"
#include <cstring>

namespace zftl {

SlotAllocation LZ4Compressor::QuantizeSlot(uint16_t bytes, bool is_zero) noexcept {
    if (is_zero || bytes == 0) return SlotAllocation::SPARSE_ZERO;
    if (bytes <= 1024) return SlotAllocation::SLOT_1KB;
    if (bytes <= 2048) return SlotAllocation::SLOT_2KB;
    if (bytes <= 3072) return SlotAllocation::SLOT_3KB;
    return SlotAllocation::FULL_4KB;
}

CompressedBlock LZ4Compressor::Compress(const void* src_4kb) noexcept {
    CompressedBlock block;
    if (!src_4kb) return block;

    const uint64_t* u64_ptr = static_cast<const uint64_t*>(src_4kb);
    constexpr size_t NUM_U64 = FLASH_PAGE_SIZE / sizeof(uint64_t); // 512 words
    bool all_zeros = true;
    for (size_t i = 0; i < NUM_U64; ++i) {
        if (u64_ptr[i] != 0) {
            all_zeros = false;
            break;
        }
    }

    if (all_zeros) {
        block.is_zero_block = true;
        block.is_compressed = true;
        block.compressed_size = 0;
        block.slot = SlotAllocation::SPARSE_ZERO;
        return block;
    }

    // Attempt LZ4 compression
    int comp_size = LZ4_compress_default(
        static_cast<const char*>(src_4kb),
        reinterpret_cast<char*>(block.data.data()),
        static_cast<int>(FLASH_PAGE_SIZE),
        static_cast<int>(FLASH_PAGE_SIZE)
    );

    // If compression failed or offered less than a 3KB slot (< 25% savings), store raw
    if (comp_size <= 0 || comp_size >= 3072) {
        block.is_compressed = false;
        block.compressed_size = static_cast<uint16_t>(FLASH_PAGE_SIZE);
        block.slot = SlotAllocation::FULL_4KB;
        std::memcpy(block.data.data(), src_4kb, FLASH_PAGE_SIZE);
    } else {
        block.is_compressed = true;
        block.compressed_size = static_cast<uint16_t>(comp_size);
        block.slot = QuantizeSlot(block.compressed_size, false);
    }

    return block;
}

bool LZ4Compressor::Decompress(const CompressedBlock& comp, void* dest_4kb) noexcept {
    if (!dest_4kb) return false;

    if (comp.is_zero_block) {
        std::memset(dest_4kb, 0, FLASH_PAGE_SIZE);
        return true;
    }

    if (!comp.is_compressed) {
        std::memcpy(dest_4kb, comp.data.data(), FLASH_PAGE_SIZE);
        return true;
    }

    int decomp_size = LZ4_decompress_safe(
        reinterpret_cast<const char*>(comp.data.data()),
        static_cast<char*>(dest_4kb),
        static_cast<int>(comp.compressed_size),
        static_cast<int>(FLASH_PAGE_SIZE)
    );

    return (decomp_size == static_cast<int>(FLASH_PAGE_SIZE));
}

} // namespace zftl
