// Copyright (c) 2026 zFTL Project. All Rights Reserved.
// Flash Translation Layer (FTL) with In-Line Real-Time Compression Implementation.
#include "ftl_controller.hpp"
#include <cstring>
#include <algorithm>

namespace zftl {

FTLController::FTLController(size_t logical_capacity_bytes, const FlashConfig& flash_cfg)
    : flash_(flash_cfg),
      lba_table_((logical_capacity_bytes + FLASH_PAGE_SIZE - 1) / FLASH_PAGE_SIZE),
      active_block_idx_(0),
      active_page_idx_(0) {
    
    // Inverted index for GC
    block_page_owners_.resize(flash_cfg.total_blocks);
    for (size_t b = 0; b < flash_cfg.total_blocks; ++b) {
        block_page_owners_[b].resize(flash_cfg.pages_per_block);
    }
}

void FTLController::FlushStagedWrites() noexcept {
    if (!staged_half_.has_value()) return;

    alignas(64) std::array<uint8_t, FLASH_PAGE_SIZE> page_buf{};
    std::memcpy(page_buf.data(), staged_half_->data.data(), 2048);

    if (active_page_idx_ >= flash_.GetPagesPerBlock()) {
        RunGarbageCollectionIfNeeded();
    }

    flash_.ProgramPage(active_block_idx_, active_page_idx_, page_buf.data());

    MappingEntry& entry = lba_table_[staged_half_->lba];
    entry.state = MappingState::COMPRESSED_HALF_PAGE;
    entry.block_idx = static_cast<uint32_t>(active_block_idx_);
    entry.page_idx = static_cast<uint32_t>(active_page_idx_);
    entry.sub_slot = 0;
    entry.compressed_size = staged_half_->comp_size;

    block_page_owners_[active_block_idx_][active_page_idx_].push_back({staged_half_->lba, 0});

    active_page_idx_++;
    staged_half_.reset();
}

bool FTLController::WriteBlock(uint64_t lba, const void* src_4kb) noexcept {
    if (lba >= lba_table_.size() || !src_4kb) return false;

    std::lock_guard<std::mutex> lock(ftl_mutex_);
    host_writes_++;

    // Invalidate existing physical allocation for this LBA if already mapped
    MappingEntry& old_entry = lba_table_[lba];
    if (old_entry.state == MappingState::RAW_FULL_PAGE || old_entry.state == MappingState::COMPRESSED_HALF_PAGE) {
        flash_.InvalidatePage(old_entry.block_idx, old_entry.page_idx);
    }

    // If writing over a currently staged LBA, flush first
    if (staged_half_.has_value() && staged_half_->lba == lba) {
        staged_half_.reset();
    }

    // In-line Compression via LZ4
    CompressedBlock comp = LZ4Compressor::Compress(src_4kb);

    // 1. Sparse Zero Optimization: Zero blocks do not touch physical flash
    if (comp.is_zero_block) {
        old_entry.state = MappingState::SPARSE_ZERO;
        old_entry.compressed_size = 0;
        zero_blocks_++;
        return true;
    }

    // 2. Half-Page Packing: Compressible to <= 2 KB
    if (comp.compressed_size <= 2048) {
        if (staged_half_.has_value()) {
            // Pair the staged half-page with this block into one 4KB physical flash page!
            alignas(64) std::array<uint8_t, FLASH_PAGE_SIZE> page_buf{};
            std::memcpy(page_buf.data(), staged_half_->data.data(), 2048);
            std::memcpy(page_buf.data() + 2048, comp.data.data(), comp.compressed_size);

            if (active_page_idx_ >= flash_.GetPagesPerBlock()) {
                RunGarbageCollectionIfNeeded();
            }

            flash_.ProgramPage(active_block_idx_, active_page_idx_, page_buf.data());

            // Map staged LBA
            MappingEntry& staged_entry = lba_table_[staged_half_->lba];
            staged_entry.state = MappingState::COMPRESSED_HALF_PAGE;
            staged_entry.block_idx = static_cast<uint32_t>(active_block_idx_);
            staged_entry.page_idx = static_cast<uint32_t>(active_page_idx_);
            staged_entry.sub_slot = 0;
            staged_entry.compressed_size = staged_half_->comp_size;

            // Map current LBA
            old_entry.state = MappingState::COMPRESSED_HALF_PAGE;
            old_entry.block_idx = static_cast<uint32_t>(active_block_idx_);
            old_entry.page_idx = static_cast<uint32_t>(active_page_idx_);
            old_entry.sub_slot = 1;
            old_entry.compressed_size = comp.compressed_size;

            block_page_owners_[active_block_idx_][active_page_idx_].push_back({staged_half_->lba, 0});
            block_page_owners_[active_block_idx_][active_page_idx_].push_back({lba, 1});

            half_page_packs_++;
            staged_half_.reset();
            active_page_idx_++;
        } else {
            // Stage this half-page and wait for a second block
            staged_half_ = StagedHalfPage{lba, comp.compressed_size, {}};
            std::memcpy(staged_half_->data.data(), comp.data.data(), comp.compressed_size);
        }
        return true;
    }

    // 3. Incompressible Full 4 KB Page
    if (staged_half_.has_value()) {
        FlushStagedWrites();
    }

    if (active_page_idx_ >= flash_.GetPagesPerBlock()) {
        RunGarbageCollectionIfNeeded();
    }

    flash_.ProgramPage(active_block_idx_, active_page_idx_, comp.data.data());

    old_entry.state = MappingState::RAW_FULL_PAGE;
    old_entry.block_idx = static_cast<uint32_t>(active_block_idx_);
    old_entry.page_idx = static_cast<uint32_t>(active_page_idx_);
    old_entry.sub_slot = 0;
    old_entry.compressed_size = comp.compressed_size;

    block_page_owners_[active_block_idx_][active_page_idx_].push_back({lba, 0});

    raw_full_pages_++;
    active_page_idx_++;
    return true;
}

bool FTLController::ReadBlock(uint64_t lba, void* dest_4kb) noexcept {
    if (lba >= lba_table_.size() || !dest_4kb) return false;

    std::lock_guard<std::mutex> lock(ftl_mutex_);
    host_reads_++;

    // Check if waiting in staging buffer
    if (staged_half_.has_value() && staged_half_->lba == lba) {
        CompressedBlock comp;
        comp.is_compressed = true;
        comp.compressed_size = staged_half_->comp_size;
        std::memcpy(comp.data.data(), staged_half_->data.data(), comp.compressed_size);
        return LZ4Compressor::Decompress(comp, dest_4kb);
    }

    const MappingEntry& entry = lba_table_[lba];

    switch (entry.state) {
        case MappingState::UNMAPPED:
            std::memset(dest_4kb, 0, FLASH_PAGE_SIZE);
            return true;

        case MappingState::SPARSE_ZERO:
            std::memset(dest_4kb, 0, FLASH_PAGE_SIZE);
            return true;

        case MappingState::RAW_FULL_PAGE:
            return flash_.ReadPage(entry.block_idx, entry.page_idx, dest_4kb);

        case MappingState::COMPRESSED_HALF_PAGE: {
            alignas(64) std::array<uint8_t, FLASH_PAGE_SIZE> flash_page{};
            if (!flash_.ReadPage(entry.block_idx, entry.page_idx, flash_page.data())) {
                return false;
            }

            CompressedBlock comp;
            comp.is_compressed = true;
            comp.compressed_size = entry.compressed_size;

            size_t offset = (entry.sub_slot == 0) ? 0 : 2048;
            std::memcpy(comp.data.data(), flash_page.data() + offset, entry.compressed_size);

            return LZ4Compressor::Decompress(comp, dest_4kb);
        }
    }

    return false;
}

void FTLController::RunGarbageCollectionIfNeeded() noexcept {
    // If active block is full, pick next free block
    if (active_page_idx_ >= flash_.GetPagesPerBlock()) {
        active_block_idx_ = (active_block_idx_ + 1) % flash_.GetTotalBlocks();
        active_page_idx_ = 0;

        // If next block is dirty (has programmed pages), reclaim it via GC
        if (flash_.IsPageProgrammed(active_block_idx_, 0)) {
            gc_count_++;

            // Find valid pages in victim block and migrate them
            size_t victim = active_block_idx_;
            for (size_t p = 0; p < flash_.GetPagesPerBlock(); ++p) {
                if (flash_.IsPageValid(victim, p)) {
                    gc_migrated_pages_++;
                    // In a full FTL, valid pages are copied to a reserve block
                }
                block_page_owners_[victim][p].clear();
            }
            flash_.EraseBlock(victim);
        }
    }
}

FTLTelemetry FTLController::GetTelemetry() const noexcept {
    std::lock_guard<std::mutex> lock(ftl_mutex_);
    FTLTelemetry tel;
    tel.total_host_writes = host_writes_;
    tel.total_host_reads = host_reads_;
    tel.total_host_bytes_written = host_writes_ * FLASH_PAGE_SIZE;
    tel.total_host_bytes_read = host_reads_ * FLASH_PAGE_SIZE;
    tel.zero_blocks_filtered = zero_blocks_;
    tel.compressed_half_pages_packed = half_page_packs_;
    tel.raw_full_pages_written = raw_full_pages_;
    tel.gc_invocations = gc_count_;
    tel.gc_pages_migrated = gc_migrated_pages_;
    tel.flash_tel = flash_.GetTelemetry();

    if (tel.total_host_bytes_written > 0) {
        tel.write_amplification_factor = static_cast<double>(tel.flash_tel.total_physical_bytes_written) /
                                         static_cast<double>(tel.total_host_bytes_written);
    }

    if (tel.flash_tel.total_physical_bytes_written > 0) {
        tel.effective_compression_ratio = static_cast<double>(tel.total_host_bytes_written) /
                                          static_cast<double>(tel.flash_tel.total_physical_bytes_written);
    }

    return tel;
}

void FTLController::ResetTelemetry() noexcept {
    std::lock_guard<std::mutex> lock(ftl_mutex_);
    host_writes_ = 0;
    host_reads_ = 0;
    zero_blocks_ = 0;
    half_page_packs_ = 0;
    raw_full_pages_ = 0;
    gc_count_ = 0;
    gc_migrated_pages_ = 0;
    flash_.ResetTelemetry();
}

} // namespace zftl
