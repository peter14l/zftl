// Copyright (c) 2026 zFTL Project. All Rights Reserved.
// Physical 3D QLC NAND Flash Array Model Implementation.
#include "flash_model.hpp"
#include <cstring>
#include <algorithm>

namespace zftl {

FlashModel::FlashModel(const FlashConfig& config)
    : config_(config) {
    blocks_.reserve(config.total_blocks);
    for (size_t i = 0; i < config.total_blocks; ++i) {
        blocks_.emplace_back(config.pages_per_block);
    }
}

bool FlashModel::ProgramPage(size_t block_idx, size_t page_idx, const void* src_4kb) noexcept {
    if (block_idx >= blocks_.size() || page_idx >= config_.pages_per_block || !src_4kb) {
        return false;
    }

    FlashBlock& blk = blocks_[block_idx];
    PhysicalPage& page = blk.pages[page_idx];

    // NAND Flash Physical Constraint: Cannot overwrite without an erase!
    if (page.is_programmed) {
        return false;
    }

    std::memcpy(page.data.data(), src_4kb, config_.page_size_bytes);
    page.is_programmed = true;
    blk.page_valid[page_idx] = true;

    total_pages_programmed_.fetch_add(1, std::memory_order_relaxed);
    
    // Atomically accumulate simulated latency
    double current = simulated_time_us_.load(std::memory_order_relaxed);
    while (!simulated_time_us_.compare_exchange_weak(current, current + config_.prog_latency_us, std::memory_order_relaxed));

    return true;
}

bool FlashModel::ReadPage(size_t block_idx, size_t page_idx, void* dest_4kb) const noexcept {
    if (block_idx >= blocks_.size() || page_idx >= config_.pages_per_block || !dest_4kb) {
        return false;
    }

    const FlashBlock& blk = blocks_[block_idx];
    const PhysicalPage& page = blk.pages[page_idx];

    if (!page.is_programmed) {
        std::memset(dest_4kb, 0xFF, config_.page_size_bytes); // Unprogrammed flash reads as 0xFF
    } else {
        std::memcpy(dest_4kb, page.data.data(), config_.page_size_bytes);
    }

    total_pages_read_.fetch_add(1, std::memory_order_relaxed);

    double current = simulated_time_us_.load(std::memory_order_relaxed);
    while (!simulated_time_us_.compare_exchange_weak(current, current + config_.read_latency_us, std::memory_order_relaxed));

    return true;
}

bool FlashModel::EraseBlock(size_t block_idx) noexcept {
    if (block_idx >= blocks_.size()) return false;

    FlashBlock& blk = blocks_[block_idx];
    blk.erase_count++;
    blk.free_page_idx = 0;
    blk.invalid_page_count = 0;

    for (size_t p = 0; p < config_.pages_per_block; ++p) {
        blk.pages[p].is_programmed = false;
        blk.page_valid[p] = false;
        std::memset(blk.pages[p].data.data(), 0xFF, config_.page_size_bytes);
    }

    total_blocks_erased_.fetch_add(1, std::memory_order_relaxed);

    double current = simulated_time_us_.load(std::memory_order_relaxed);
    while (!simulated_time_us_.compare_exchange_weak(current, current + config_.erase_latency_us, std::memory_order_relaxed));

    return true;
}

void FlashModel::InvalidatePage(size_t block_idx, size_t page_idx) noexcept {
    if (block_idx >= blocks_.size() || page_idx >= config_.pages_per_block) return;

    FlashBlock& blk = blocks_[block_idx];
    if (blk.page_valid[page_idx]) {
        blk.page_valid[page_idx] = false;
        blk.invalid_page_count++;
    }
}

bool FlashModel::IsPageProgrammed(size_t block_idx, size_t page_idx) const noexcept {
    if (block_idx >= blocks_.size() || page_idx >= config_.pages_per_block) return false;
    return blocks_[block_idx].pages[page_idx].is_programmed;
}

bool FlashModel::IsPageValid(size_t block_idx, size_t page_idx) const noexcept {
    if (block_idx >= blocks_.size() || page_idx >= config_.pages_per_block) return false;
    return blocks_[block_idx].page_valid[page_idx];
}

uint32_t FlashModel::GetEraseCount(size_t block_idx) const noexcept {
    if (block_idx >= blocks_.size()) return 0;
    return blocks_[block_idx].erase_count;
}

uint32_t FlashModel::GetInvalidCount(size_t block_idx) const noexcept {
    if (block_idx >= blocks_.size()) return 0;
    return blocks_[block_idx].invalid_page_count;
}

FlashTelemetry FlashModel::GetTelemetry() const noexcept {
    FlashTelemetry tel;
    tel.total_pages_programmed = total_pages_programmed_.load(std::memory_order_relaxed);
    tel.total_pages_read = total_pages_read_.load(std::memory_order_relaxed);
    tel.total_blocks_erased = total_blocks_erased_.load(std::memory_order_relaxed);
    tel.total_physical_bytes_written = tel.total_pages_programmed * config_.page_size_bytes;
    tel.total_simulated_time_ms = simulated_time_us_.load(std::memory_order_relaxed) / 1000.0;

    uint32_t max_e = 0;
    uint64_t sum_e = 0;
    for (const auto& blk : blocks_) {
        max_e = std::max(max_e, blk.erase_count);
        sum_e += blk.erase_count;
    }
    tel.max_pe_cycles = max_e;
    tel.avg_pe_cycles = blocks_.empty() ? 0.0 : static_cast<double>(sum_e) / blocks_.size();

    return tel;
}

void FlashModel::ResetTelemetry() noexcept {
    total_pages_programmed_.store(0, std::memory_order_relaxed);
    total_pages_read_.store(0, std::memory_order_relaxed);
    total_blocks_erased_.store(0, std::memory_order_relaxed);
    simulated_time_us_.store(0.0, std::memory_order_relaxed);
}

} // namespace zftl
