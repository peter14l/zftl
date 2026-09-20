// Copyright (c) 2026 HyperRAM Project. All Rights Reserved.
// Open-Hardware / Low-Cost Memory Architecture Initiative.
#include "hyper_ram_controller.hpp"
#include <cstring>
#include <algorithm>

namespace hyper_ram {

HyperRAMController::HyperRAMController(const ControllerConfig& config)
    : config_(config),
      line_table_(std::make_unique<MemoryLineTable>(config.virtual_capacity_bytes, config.physical_dram_bytes)) {
    ResetTelemetry();
}

void HyperRAMController::ResetTelemetry() noexcept {
    total_reads_.store(0, std::memory_order_relaxed);
    total_writes_.store(0, std::memory_order_relaxed);
    total_bytes_read_.store(0, std::memory_order_relaxed);
    total_bytes_written_.store(0, std::memory_order_relaxed);
    total_read_latency_accum_ns_.store(0, std::memory_order_relaxed);
    total_write_latency_accum_ns_.store(0, std::memory_order_relaxed);
    for (auto& count : pattern_counts_) {
        count.store(0, std::memory_order_relaxed);
    }
}

bool HyperRAMController::Read(uint64_t byte_address, void* dest, size_t size) noexcept {
    if (!dest || size == 0) return false;
    if (byte_address > config_.virtual_capacity_bytes || size > config_.virtual_capacity_bytes - byte_address) {
        return false;
    }

    uint8_t* dest_bytes = static_cast<uint8_t*>(dest);
    uint64_t start_addr = byte_address;
    uint64_t end_addr = byte_address + size;
    size_t dest_offset = 0;

    alignas(64) std::array<uint8_t, CACHE_LINE_SIZE> line_buf{};

    while (start_addr < end_addr) {
        uint64_t line_idx = start_addr / CACHE_LINE_SIZE;
        uint64_t line_offset = start_addr % CACHE_LINE_SIZE;
        size_t to_copy = std::min<size_t>(static_cast<size_t>(end_addr - start_addr),
                                          static_cast<size_t>(CACHE_LINE_SIZE - line_offset));

        if (!line_table_->LoadLine(line_idx, line_buf.data())) {
            return false;
        }

        std::memcpy(dest_bytes + dest_offset, line_buf.data() + line_offset, to_copy);

        dest_offset += to_copy;
        start_addr += to_copy;

        // Account for latency
        uint64_t lat = static_cast<uint64_t>(config_.dram_base_latency_ns + config_.decompression_latency_ns);
        total_read_latency_accum_ns_.fetch_add(lat, std::memory_order_relaxed);
    }

    total_reads_.fetch_add(1, std::memory_order_relaxed);
    total_bytes_read_.fetch_add(size, std::memory_order_relaxed);
    return true;
}

bool HyperRAMController::Write(uint64_t byte_address, const void* src, size_t size) noexcept {
    if (!src || size == 0) return false;
    if (byte_address > config_.virtual_capacity_bytes || size > config_.virtual_capacity_bytes - byte_address) {
        return false;
    }

    const uint8_t* src_bytes = static_cast<const uint8_t*>(src);
    uint64_t start_addr = byte_address;
    uint64_t end_addr = byte_address + size;
    size_t src_offset = 0;

    alignas(64) std::array<uint8_t, CACHE_LINE_SIZE> line_buf{};

    while (start_addr < end_addr) {
        uint64_t line_idx = start_addr / CACHE_LINE_SIZE;
        uint64_t line_offset = start_addr % CACHE_LINE_SIZE;
        size_t to_write = std::min<size_t>(static_cast<size_t>(end_addr - start_addr),
                                           static_cast<size_t>(CACHE_LINE_SIZE - line_offset));

        if (line_offset == 0 && to_write == CACHE_LINE_SIZE) {
            // Fast-path: Entire 64-byte line is overwritten directly
            std::memcpy(line_buf.data(), src_bytes + src_offset, CACHE_LINE_SIZE);
        } else {
            // Read-Modify-Write: Load existing line first
            if (!line_table_->LoadLine(line_idx, line_buf.data())) {
                return false;
            }
            std::memcpy(line_buf.data() + line_offset, src_bytes + src_offset, to_write);
        }

        // Compress line via BDI
        CompressedLine comp = BDIEngine::Compress(line_buf.data());

        // Update pattern distribution
        uint8_t pat_idx = static_cast<uint8_t>(comp.pattern) & 0xF;
        pattern_counts_[pat_idx].fetch_add(1, std::memory_order_relaxed);

        // Store into physical memory line table
        if (!line_table_->StoreLine(line_idx, comp)) {
            // Physical memory full (cannot accommodate compressed block)
            return false;
        }

        src_offset += to_write;
        start_addr += to_write;

        // Account for write latency (DRAM write + BDI compression pipeline)
        uint64_t lat = static_cast<uint64_t>(config_.dram_base_latency_ns + config_.compression_latency_ns);
        total_write_latency_accum_ns_.fetch_add(lat, std::memory_order_relaxed);
    }

    total_writes_.fetch_add(1, std::memory_order_relaxed);
    total_bytes_written_.fetch_add(size, std::memory_order_relaxed);
    return true;
}

MemoryTelemetry HyperRAMController::GetTelemetry() const noexcept {
    MemoryTelemetry tel;
    tel.total_reads = total_reads_.load(std::memory_order_relaxed);
    tel.total_writes = total_writes_.load(std::memory_order_relaxed);
    tel.total_bytes_read = total_bytes_read_.load(std::memory_order_relaxed);
    tel.total_bytes_written = total_bytes_written_.load(std::memory_order_relaxed);
    tel.physical_bytes_stored = line_table_->GetAllocatedPhysicalBytes();
    tel.virtual_bytes_mapped = line_table_->GetTotalLogicalLines() * CACHE_LINE_SIZE;
    tel.compression_ratio = line_table_->GetCurrentCompressionRatio();
    tel.memory_savings_pct = line_table_->GetMemorySavingPercentage();

    uint64_t r_ops = tel.total_reads;
    tel.avg_read_latency_ns = (r_ops > 0) ? static_cast<double>(total_read_latency_accum_ns_.load(std::memory_order_relaxed)) / r_ops : 0.0;

    uint64_t w_ops = tel.total_writes;
    tel.avg_write_latency_ns = (w_ops > 0) ? static_cast<double>(total_write_latency_accum_ns_.load(std::memory_order_relaxed)) / w_ops : 0.0;

    for (size_t i = 0; i < 16; ++i) {
        tel.pattern_distribution[i] = pattern_counts_[i].load(std::memory_order_relaxed);
    }

    return tel;
}

} // namespace hyper_ram
