// Copyright (c) 2026 HyperRAM Project. All Rights Reserved.
// Open-Hardware / Low-Cost Memory Architecture Initiative.
#pragma once

#include "bdi_engine.hpp"
#include "line_table.hpp"
#include "telemetry.hpp"
#include <cstdint>
#include <cstddef>
#include <memory>
#include <atomic>
#include <mutex>
#include <array>

namespace hyper_ram {

struct ControllerConfig {
    size_t virtual_capacity_bytes = 16ULL * 1024 * 1024 * 1024; // 16 GB Virtual RAM
    size_t physical_dram_bytes   = 8ULL * 1024 * 1024 * 1024;  // 8 GB Physical DRAM
    double dram_base_latency_ns  = 45.0;                        // DDR5 base access latency
    double decompression_latency_ns = 5.0;                      // Hardware BDI pipeline latency
    double compression_latency_ns   = 8.0;                      // Hardware BDI encoder latency
    bool enable_latency_injection = false;
};

class HyperRAMController {
public:
    explicit HyperRAMController(const ControllerConfig& config = ControllerConfig{});
    ~HyperRAMController() = default;

    // Disallow copies
    HyperRAMController(const HyperRAMController&) = delete;
    HyperRAMController& operator=(const HyperRAMController&) = delete;

    // Byte-addressable Read: seamlessly manages 64-byte line decompression
    bool Read(uint64_t byte_address, void* dest, size_t size) noexcept;

    // Byte-addressable Write: compresses modified 64-byte lines and compacts DRAM
    bool Write(uint64_t byte_address, const void* src, size_t size) noexcept;

    // Telemetry & Statistics
    MemoryTelemetry GetTelemetry() const noexcept;
    void ResetTelemetry() noexcept;

    // Direct access to internal Line Table
    const MemoryLineTable& GetLineTable() const noexcept { return *line_table_; }

private:
    ControllerConfig config_;
    std::unique_ptr<MemoryLineTable> line_table_;

    // Performance telemetry
    mutable std::atomic<uint64_t> total_reads_{0};
    mutable std::atomic<uint64_t> total_writes_{0};
    mutable std::atomic<uint64_t> total_bytes_read_{0};
    mutable std::atomic<uint64_t> total_bytes_written_{0};
    mutable std::atomic<uint64_t> total_read_latency_accum_ns_{0};
    mutable std::atomic<uint64_t> total_write_latency_accum_ns_{0};
    mutable std::array<std::atomic<uint64_t>, 16> pattern_counts_{};
};

} // namespace hyper_ram
