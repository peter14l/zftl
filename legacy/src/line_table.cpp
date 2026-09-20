// Copyright (c) 2026 HyperRAM Project. All Rights Reserved.
// Open-Hardware / Low-Cost Memory Architecture Initiative.
#include "line_table.hpp"
#include <cstring>
#include <algorithm>

namespace hyper_ram {

MemoryLineTable::MemoryLineTable(size_t virtual_capacity, size_t physical_capacity)
    : virtual_capacity_(virtual_capacity),
      physical_capacity_(physical_capacity),
      total_logical_lines_((virtual_capacity + CACHE_LINE_SIZE - 1) / CACHE_LINE_SIZE),
      total_physical_chunks_(physical_capacity / SLOT_CHUNK_SIZE) {
    
    // Allocate Line Table (8 bytes per 64-byte logical line)
    entries_.resize(total_logical_lines_);
    std::memset(entries_.data(), 0, entries_.size() * sizeof(LineTableEntry));

    // Allocate physical DRAM buffer
    physical_dram_ = std::make_unique<uint8_t[]>(physical_capacity_);
    std::memset(physical_dram_.get(), 0, physical_capacity_);

    Reset();
}

void MemoryLineTable::Reset() noexcept {
    std::lock_guard<std::mutex> lock(alloc_mutex_);
    std::memset(entries_.data(), 0, entries_.size() * sizeof(LineTableEntry));
    std::memset(physical_dram_.get(), 0, physical_capacity_);

    free_chunks_16b_.clear();
    free_chunks_32b_.clear();
    free_chunks_48b_.clear();
    free_chunks_64b_.clear();
    next_unpartitioned_chunk_ = 0;

    allocated_chunks_count_.store(0, std::memory_order_relaxed);
    total_uncompressed_bytes_stored_.store(0, std::memory_order_relaxed);
}

uint32_t MemoryLineTable::AllocateChunks(uint8_t num_chunks) noexcept {
    if (num_chunks == 0) return 0;

    // 1. Check specific free lists
    if (num_chunks == 1 && !free_chunks_16b_.empty()) {
        uint32_t offset = free_chunks_16b_.back();
        free_chunks_16b_.pop_back();
        allocated_chunks_count_.fetch_add(num_chunks, std::memory_order_relaxed);
        return offset;
    }
    if (num_chunks == 2 && !free_chunks_32b_.empty()) {
        uint32_t offset = free_chunks_32b_.back();
        free_chunks_32b_.pop_back();
        allocated_chunks_count_.fetch_add(num_chunks, std::memory_order_relaxed);
        return offset;
    }
    if (num_chunks == 3 && !free_chunks_48b_.empty()) {
        uint32_t offset = free_chunks_48b_.back();
        free_chunks_48b_.pop_back();
        allocated_chunks_count_.fetch_add(num_chunks, std::memory_order_relaxed);
        return offset;
    }
    if (num_chunks == 4 && !free_chunks_64b_.empty()) {
        uint32_t offset = free_chunks_64b_.back();
        free_chunks_64b_.pop_back();
        allocated_chunks_count_.fetch_add(num_chunks, std::memory_order_relaxed);
        return offset;
    }

    // 2. Allocate from unpartitioned bump pointer
    if (next_unpartitioned_chunk_ + num_chunks <= total_physical_chunks_) {
        uint32_t offset = static_cast<uint32_t>(next_unpartitioned_chunk_);
        next_unpartitioned_chunk_ += num_chunks;
        allocated_chunks_count_.fetch_add(num_chunks, std::memory_order_relaxed);
        return offset;
    }

    // Out of memory
    return UINT32_MAX;
}

void MemoryLineTable::FreeChunks(uint32_t chunk_offset, uint8_t num_chunks) noexcept {
    if (num_chunks == 0 || chunk_offset == UINT32_MAX) return;

    allocated_chunks_count_.fetch_sub(num_chunks, std::memory_order_relaxed);

    if (num_chunks == 1) free_chunks_16b_.push_back(chunk_offset);
    else if (num_chunks == 2) free_chunks_32b_.push_back(chunk_offset);
    else if (num_chunks == 3) free_chunks_48b_.push_back(chunk_offset);
    else if (num_chunks == 4) free_chunks_64b_.push_back(chunk_offset);
}

bool MemoryLineTable::StoreLine(uint64_t logical_line_idx, const CompressedLine& comp_line) noexcept {
    if (logical_line_idx >= total_logical_lines_) return false;

    uint8_t needed_chunks = 0;
    if (comp_line.pattern != BDIPattern::ZEROS) {
        uint8_t slot_bytes = BDIEngine::GetQuantizedSlotSize(comp_line.compressed_size);
        needed_chunks = slot_bytes / static_cast<uint8_t>(SLOT_CHUNK_SIZE);
    }

    std::lock_guard<std::mutex> lock(alloc_mutex_);
    LineTableEntry& entry = entries_[logical_line_idx];

    // Check if we can reuse the existing allocation
    uint32_t chunk_offset = entry.phys_chunk_offset;
    if (entry.valid) {
        if (entry.allocated_chunks != needed_chunks) {
            FreeChunks(entry.phys_chunk_offset, static_cast<uint8_t>(entry.allocated_chunks));
            chunk_offset = AllocateChunks(needed_chunks);
            if (needed_chunks > 0 && chunk_offset == UINT32_MAX) {
                // Out of physical DRAM space!
                return false;
            }
        }
    } else {
        chunk_offset = AllocateChunks(needed_chunks);
        if (needed_chunks > 0 && chunk_offset == UINT32_MAX) {
            return false;
        }
        total_uncompressed_bytes_stored_.fetch_add(CACHE_LINE_SIZE, std::memory_order_relaxed);
    }

    // Write compressed data into physical DRAM
    if (needed_chunks > 0) {
        size_t byte_dest = static_cast<size_t>(chunk_offset) * SLOT_CHUNK_SIZE;
        std::memcpy(physical_dram_.get() + byte_dest, comp_line.data.data(), comp_line.compressed_size);
    }

    // Update table entry
    entry.phys_chunk_offset = (needed_chunks > 0) ? chunk_offset : 0;
    entry.allocated_chunks = needed_chunks;
    entry.pattern_tag = static_cast<uint32_t>(comp_line.pattern);
    entry.valid = 1;

    return true;
}

bool MemoryLineTable::LoadLine(uint64_t logical_line_idx, void* dest_64b) const noexcept {
    if (logical_line_idx >= total_logical_lines_ || !dest_64b) return false;

    LineTableEntry entry;
    {
        std::lock_guard<std::mutex> lock(alloc_mutex_);
        entry = entries_[logical_line_idx];
    }

    if (!entry.valid) {
        std::memset(dest_64b, 0, CACHE_LINE_SIZE);
        return true;
    }

    if (entry.allocated_chunks == 0 || entry.pattern_tag == static_cast<uint32_t>(BDIPattern::ZEROS)) {
        std::memset(dest_64b, 0, CACHE_LINE_SIZE);
        return true;
    }

    size_t byte_src = static_cast<size_t>(entry.phys_chunk_offset) * SLOT_CHUNK_SIZE;
    size_t comp_size = entry.allocated_chunks * SLOT_CHUNK_SIZE;

    return BDIEngine::DecompressRaw(physical_dram_.get() + byte_src, comp_size, dest_64b, static_cast<BDIPattern>(entry.pattern_tag));
}

LineTableEntry MemoryLineTable::GetEntry(uint64_t logical_line_idx) const noexcept {
    if (logical_line_idx >= total_logical_lines_) return LineTableEntry{};
    std::lock_guard<std::mutex> lock(alloc_mutex_);
    return entries_[logical_line_idx];
}

size_t MemoryLineTable::GetAllocatedPhysicalBytes() const noexcept {
    return allocated_chunks_count_.load(std::memory_order_relaxed) * SLOT_CHUNK_SIZE;
}

double MemoryLineTable::GetCurrentCompressionRatio() const noexcept {
    size_t phys_bytes = GetAllocatedPhysicalBytes();
    size_t uncomp_bytes = total_uncompressed_bytes_stored_.load(std::memory_order_relaxed);
    if (phys_bytes == 0) return (uncomp_bytes > 0) ? 999.0 : 1.0;
    return static_cast<double>(uncomp_bytes) / static_cast<double>(phys_bytes);
}

double MemoryLineTable::GetMemorySavingPercentage() const noexcept {
    size_t phys_bytes = GetAllocatedPhysicalBytes();
    size_t uncomp_bytes = total_uncompressed_bytes_stored_.load(std::memory_order_relaxed);
    if (uncomp_bytes == 0) return 0.0;
    if (phys_bytes >= uncomp_bytes) return 0.0;
    return (1.0 - (static_cast<double>(phys_bytes) / static_cast<double>(uncomp_bytes))) * 100.0;
}

} // namespace hyper_ram
