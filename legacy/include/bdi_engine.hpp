// Copyright (c) 2026 HyperRAM Project. All Rights Reserved.
// Open-Hardware / Low-Cost Memory Architecture Initiative.
#pragma once

#include <cstdint>
#include <cstddef>
#include <array>
#include <string_view>
#include <optional>

namespace hyper_ram {

// Maximum size of a CPU cache line
inline constexpr size_t CACHE_LINE_SIZE = 64;

// Compression Pattern Encoding Tag
enum class BDIPattern : uint8_t {
    ZEROS        = 0x0, // Line is all zeros (compressed to 1 byte tag)
    REP_WORD     = 0x1, // Entire line is single repeated 8-byte word (compressed to 9 bytes)
    BASE8_DELTA1 = 0x2, // 8-byte base + 7x 1-byte signed deltas (15 bytes payload)
    BASE8_DELTA2 = 0x3, // 8-byte base + 7x 2-byte signed deltas (22 bytes payload)
    BASE8_DELTA4 = 0x4, // 8-byte base + 7x 4-byte signed deltas (36 bytes payload)
    BASE4_DELTA1 = 0x5, // 4-byte base + 15x 1-byte signed deltas (19 bytes payload)
    BASE4_DELTA2 = 0x6, // 4-byte base + 15x 2-byte signed deltas (34 bytes payload)
    BASE2_DELTA1 = 0x7, // 2-byte base + 31x 1-byte signed deltas (33 bytes payload)
    UNCOMPRESSED = 0xF  // Incompressible; stored as raw 64 bytes
};

// Returns human-readable name of the BDI pattern
constexpr std::string_view PatternToString(BDIPattern pattern) noexcept {
    switch (pattern) {
        case BDIPattern::ZEROS:        return "Zeros";
        case BDIPattern::REP_WORD:     return "Repeated-Word";
        case BDIPattern::BASE8_DELTA1: return "Base8-Delta1";
        case BDIPattern::BASE8_DELTA2: return "Base8-Delta2";
        case BDIPattern::BASE8_DELTA4: return "Base8-Delta4";
        case BDIPattern::BASE4_DELTA1: return "Base4-Delta1";
        case BDIPattern::BASE4_DELTA2: return "Base4-Delta2";
        case BDIPattern::BASE2_DELTA1: return "Base2-Delta1";
        case BDIPattern::UNCOMPRESSED: return "Uncompressed";
        default:                       return "Unknown";
    }
}

// Compressed line representation
struct CompressedLine {
    BDIPattern pattern{BDIPattern::UNCOMPRESSED};
    uint8_t compressed_size{CACHE_LINE_SIZE}; // Size in bytes (1 to 64)
    std::array<uint8_t, CACHE_LINE_SIZE + 1> data{}; // Tag + payload
};

class BDIEngine {
public:
    // Compresses a 64-byte cache line using the optimal BDI pattern
    static CompressedLine Compress(const void* src_64b) noexcept;

    // Decompresses a CompressedLine back into a 64-byte line
    // Returns true on success, false if corrupted
    static bool Decompress(const CompressedLine& comp, void* dest_64b) noexcept;

    // Raw buffer decompressor (reads directly from tag + payload buffer or explicit pattern)
    static bool DecompressRaw(const uint8_t* comp_buffer, size_t comp_size, void* dest_64b, std::optional<BDIPattern> explicit_pattern = std::nullopt) noexcept;

    // Helper: returns the allocated slot size (16, 32, 48, or 64 bytes)
    static uint8_t GetQuantizedSlotSize(uint8_t raw_compressed_size) noexcept;
};

} // namespace hyper_ram
