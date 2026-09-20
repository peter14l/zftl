// Copyright (c) 2026 HyperRAM Project. All Rights Reserved.
// Open-Hardware / Low-Cost Memory Architecture Initiative.
#pragma once

#include <cstdint>
#include <array>
#include <atomic>

namespace hyper_ram {

struct MemoryTelemetry {
    uint64_t total_reads{0};
    uint64_t total_writes{0};
    uint64_t total_bytes_read{0};
    uint64_t total_bytes_written{0};
    uint64_t physical_bytes_stored{0};
    uint64_t virtual_bytes_mapped{0};
    double compression_ratio{1.0};
    double memory_savings_pct{0.0};
    double avg_read_latency_ns{0.0};
    double avg_write_latency_ns{0.0};
    std::array<uint64_t, 16> pattern_distribution{};
};

} // namespace hyper_ram
