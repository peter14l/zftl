// Copyright (c) 2026 HyperRAM Project. All Rights Reserved.
// Open-Hardware / Low-Cost Memory Architecture Initiative.
#pragma once

#include "bdi_engine.hpp"
#include <cstdint>
#include <cstddef>
#include <vector>
#include <memory>
#include <mutex>
#include <atomic>

namespace hyper_ram {

// 16-byte slot quantization
inline constexpr size_t SLOT_CHUNK_SIZE = 16;
inline constexpr size_t CHUNKS_PER_LINE = CACHE_LINE_SIZE / SLOT_CHUNK_SIZE; // 4 chunks

// Memory Line Table Entry (64 bits per 64-byte cache line = 1.56% metadata overhead)
struct alignas(8) LineTableEntry {
    uint32_t phys_chunk_offset : 24; // Points to 16-byte chunk index in DRAM buffer (supports 256GB DRAM)
    uint32_t allocated_chunks  : 3;  // 0 (zeros), 1 (16B), 2 (32B), 3 (48B), 4 (64B)
    uint32_t pattern_tag       : 4;  // BDIPattern enum value
    uint32_t valid             : 1;  // 1 if line contains valid data
    uint32_t reserved          : 32; // Available for future ECC or access frequency
};
static_assert(sizeof(LineTableEntry) == 8, "LineTableEntry must be exactly 8 bytes");

class MemoryLineTable {
public:
    // virtual_capacity: size of virtual address space (e.g. 16 GB)
    // physical_capacity: size of actual physical DRAM buffer (e.g. 8 GB)
    MemoryLineTable(size_t virtual_capacity, size_t physical_capacity);
    ~MemoryLineTable() = default;

    // Disallow copies
    MemoryLineTable(const MemoryLineTable&) = delete;
    MemoryLineTable& operator=(const MemoryLineTable&) = delete;

    // Allocate/Map a compressed line
    bool StoreLine(uint64_t logical_line_idx, const CompressedLine& comp_line) noexcept;

    // Retrieve and decompress a line
    bool LoadLine(uint64_t logical_line_idx, void* dest_64b) const noexcept;

    // Query entry
    LineTableEntry GetEntry(uint64_t logical_line_idx) const noexcept;

    // Capacity metrics
    size_t GetVirtualCapacity() const noexcept { return virtual_capacity_; }
    size_t GetPhysicalCapacity() const noexcept { return physical_capacity_; }
    size_t GetTotalLogicalLines() const noexcept { return total_logical_lines_; }
    size_t GetAllocatedPhysicalBytes() const noexcept;
    double GetCurrentCompressionRatio() const noexcept;
    double GetMemorySavingPercentage() const noexcept;

    // Reset table
    void Reset() noexcept;

private:
    size_t virtual_capacity_;
    size_t physical_capacity_;
    size_t total_logical_lines_;
    size_t total_physical_chunks_; // in 16-byte chunks

    std::vector<LineTableEntry> entries_;
    std::unique_ptr<uint8_t[]> physical_dram_;

    // Fast lock-free or mutex-guarded chunk allocator
    mutable std::mutex alloc_mutex_;
    std::vector<uint32_t> free_chunks_16b_;
    std::vector<uint32_t> free_chunks_32b_;
    std::vector<uint32_t> free_chunks_48b_;
    std::vector<uint32_t> free_chunks_64b_;
    size_t next_unpartitioned_chunk_{0};

    uint32_t AllocateChunks(uint8_t num_chunks) noexcept;
    void FreeChunks(uint32_t chunk_offset, uint8_t num_chunks) noexcept;

    std::atomic<uint64_t> allocated_chunks_count_{0};
    std::atomic<uint64_t> total_uncompressed_bytes_stored_{0};
};

} // namespace hyper_ram
