// Copyright (c) 2026 zFTL Project. All Rights Reserved.
// Physical 3D QLC NAND Flash Array Model.
#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>
#include <array>
#include <atomic>
#include <string>

namespace zftl {

struct FlashConfig {
    size_t page_size_bytes{4096};     // 4 KB page
    size_t pages_per_block{64};       // 64 pages per block (256 KB block)
    size_t total_blocks{256};         // 256 physical blocks (64 MB simulated pool)
    uint32_t max_pe_cycles{500};      // Standard QLC rated endurance (P/E cycles)
    double prog_latency_us{800.0};    // QLC page program latency
    double read_latency_us{50.0};     // QLC page read latency
    double erase_latency_us{3500.0};  // Block erase latency
};

struct PhysicalPage {
    bool is_programmed{false};
    std::array<uint8_t, 4096> data{};
};

struct FlashBlock {
    uint32_t erase_count{0};
    uint32_t free_page_idx{0};        // Next sequential page to write in this block
    uint32_t invalid_page_count{0};   // Count of stale pages marked for GC
    std::vector<PhysicalPage> pages;
    std::vector<bool> page_valid;     // Tracks if each page contains live or stale data

    explicit FlashBlock(size_t num_pages)
        : pages(num_pages), page_valid(num_pages, false) {}
};

struct FlashTelemetry {
    uint64_t total_pages_programmed{0};
    uint64_t total_pages_read{0};
    uint64_t total_blocks_erased{0};
    uint64_t total_physical_bytes_written{0};
    double max_pe_cycles{0};
    double avg_pe_cycles{0};
    double total_simulated_time_ms{0.0};
};

class FlashModel {
public:
    explicit FlashModel(const FlashConfig& config);

    // NAND Physical Operations
    bool ProgramPage(size_t block_idx, size_t page_idx, const void* src_4kb) noexcept;
    bool ReadPage(size_t block_idx, size_t page_idx, void* dest_4kb) const noexcept;
    bool EraseBlock(size_t block_idx) noexcept;
    void InvalidatePage(size_t block_idx, size_t page_idx) noexcept;

    // Block Queries
    bool IsPageProgrammed(size_t block_idx, size_t page_idx) const noexcept;
    bool IsPageValid(size_t block_idx, size_t page_idx) const noexcept;
    uint32_t GetEraseCount(size_t block_idx) const noexcept;
    uint32_t GetInvalidCount(size_t block_idx) const noexcept;
    size_t GetTotalBlocks() const noexcept { return config_.total_blocks; }
    size_t GetPagesPerBlock() const noexcept { return config_.pages_per_block; }

    FlashTelemetry GetTelemetry() const noexcept;
    void ResetTelemetry() noexcept;

private:
    FlashConfig config_;
    std::vector<FlashBlock> blocks_;

    mutable std::atomic<uint64_t> total_pages_programmed_{0};
    mutable std::atomic<uint64_t> total_pages_read_{0};
    mutable std::atomic<uint64_t> total_blocks_erased_{0};
    mutable std::atomic<double> simulated_time_us_{0.0};
};

} // namespace zftl
