// Copyright (c) 2026 zFTL Project. All Rights Reserved.
// Flash Translation Layer (FTL) with In-Line Real-Time Compression.
#pragma once

#include "flash_model.hpp"
#include "lz4_compressor.hpp"
#include <vector>
#include <memory>
#include <mutex>
#include <optional>

namespace zftl {

enum class MappingState : uint8_t {
    UNMAPPED = 0,
    SPARSE_ZERO,
    COMPRESSED_HALF_PAGE, // Packed into 2KB sub-slot
    RAW_FULL_PAGE         // Uncompressed 4KB page
};

struct MappingEntry {
    MappingState state{MappingState::UNMAPPED};
    uint32_t block_idx{0};
    uint32_t page_idx{0};
    uint8_t sub_slot{0};         // 0 = first 2KB, 1 = second 2KB
    uint16_t compressed_size{0}; // Payload size
};

struct FTLTelemetry {
    uint64_t total_host_writes{0};
    uint64_t total_host_reads{0};
    uint64_t total_host_bytes_written{0};
    uint64_t total_host_bytes_read{0};
    uint64_t zero_blocks_filtered{0};
    uint64_t compressed_half_pages_packed{0};
    uint64_t raw_full_pages_written{0};
    uint64_t gc_invocations{0};
    uint64_t gc_pages_migrated{0};
    double write_amplification_factor{0.0};
    double effective_compression_ratio{0.0};
    FlashTelemetry flash_tel;
};

class FTLController {
public:
    FTLController(size_t logical_capacity_bytes, const FlashConfig& flash_cfg);

    // Host Storage Operations (4 KB LBA granularity)
    bool WriteBlock(uint64_t lba, const void* src_4kb) noexcept;
    bool ReadBlock(uint64_t lba, void* dest_4kb) noexcept;
    void FlushStagedWrites() noexcept;

    // Telemetry & Reporting
    FTLTelemetry GetTelemetry() const noexcept;
    void ResetTelemetry() noexcept;

    size_t GetTotalLBAs() const noexcept { return lba_table_.size(); }

private:
    FlashModel flash_;
    std::vector<MappingEntry> lba_table_;

    // Active allocation block and page pointer
    size_t active_block_idx_{0};
    size_t active_page_idx_{0};

    // Staging buffer for packing two 2KB compressed half-pages into one 4KB flash page
    struct StagedHalfPage {
        uint64_t lba{0};
        uint16_t comp_size{0};
        std::array<uint8_t, 2048> data{};
    };
    std::optional<StagedHalfPage> staged_half_;

    // Inverted index for GC page migration: (block, page) -> vector of (lba, sub_slot)
    struct PageOwner {
        uint64_t lba{0};
        uint8_t sub_slot{0};
    };
    std::vector<std::vector<std::vector<PageOwner>>> block_page_owners_;

    // Garbage Collection
    void RunGarbageCollectionIfNeeded() noexcept;
    size_t AllocatePhysicalPage() noexcept; // Returns (block, page) index

    mutable std::mutex ftl_mutex_;

    // Telemetry counters
    uint64_t host_writes_{0};
    uint64_t host_reads_{0};
    uint64_t zero_blocks_{0};
    uint64_t half_page_packs_{0};
    uint64_t raw_full_pages_{0};
    uint64_t gc_count_{0};
    uint64_t gc_migrated_pages_{0};
};

} // namespace zftl
