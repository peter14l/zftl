// Copyright (c) 2026 HyperRAM Project. All Rights Reserved.
// Open-Hardware / Low-Cost Memory Architecture Initiative.
#include "bdi_engine.hpp"
#include <cstring>
#include <limits>
#include <algorithm>

namespace hyper_ram {

uint8_t BDIEngine::GetQuantizedSlotSize(uint8_t raw_compressed_size) noexcept {
    if (raw_compressed_size <= 16) return 16;
    if (raw_compressed_size <= 32) return 32;
    if (raw_compressed_size <= 48) return 48;
    return 64;
}

CompressedLine BDIEngine::Compress(const void* src_64b) noexcept {
    CompressedLine result;
    if (!src_64b) return result;

    // Prepare representations
    std::array<int64_t, 8> words64;
    std::memcpy(words64.data(), src_64b, 64);

    // 1. Check All ZEROS (1-byte tag)
    bool all_zero = std::all_of(words64.begin(), words64.end(), [](int64_t w) { return w == 0; });
    if (all_zero) {
        result.pattern = BDIPattern::ZEROS;
        result.compressed_size = 1;
        result.data[0] = static_cast<uint8_t>(BDIPattern::ZEROS);
        return result;
    }

    std::array<int32_t, 16> words32;
    std::memcpy(words32.data(), src_64b, 64);

    std::array<int16_t, 32> words16;
    std::memcpy(words16.data(), src_64b, 64);

    // 2. Check Repeated Word (8-byte base + tag = 9 bytes)
    bool repeated = true;
    for (size_t i = 1; i < 8; ++i) {
        if (words64[i] != words64[0]) {
            repeated = false;
            break;
        }
    }
    if (repeated) {
        result.pattern = BDIPattern::REP_WORD;
        result.compressed_size = 9;
        result.data[0] = static_cast<uint8_t>(BDIPattern::REP_WORD);
        std::memcpy(&result.data[1], &words64[0], 8);
        return result;
    }

    // 3. Evaluate Base8-Delta1 (8-byte base + 7x 1-byte delta = 16 bytes)
    bool b8d1_ok = true;
    for (size_t i = 1; i < 8; ++i) {
        int64_t diff = words64[i] - words64[0];
        if (diff < std::numeric_limits<int8_t>::min() || diff > std::numeric_limits<int8_t>::max()) {
            b8d1_ok = false;
            break;
        }
    }
    if (b8d1_ok) {
        result.pattern = BDIPattern::BASE8_DELTA1;
        result.compressed_size = 16;
        result.data[0] = static_cast<uint8_t>(BDIPattern::BASE8_DELTA1);
        std::memcpy(&result.data[1], &words64[0], 8);
        for (size_t i = 1; i < 8; ++i) {
            result.data[9 + (i - 1)] = static_cast<uint8_t>(static_cast<int8_t>(words64[i] - words64[0]));
        }
        return result;
    }

    // 4. Evaluate Base4-Delta1 (4-byte base + 15x 1-byte delta = 20 bytes)
    bool b4d1_ok = true;
    for (size_t i = 1; i < 16; ++i) {
        int32_t diff = words32[i] - words32[0];
        if (diff < std::numeric_limits<int8_t>::min() || diff > std::numeric_limits<int8_t>::max()) {
            b4d1_ok = false;
            break;
        }
    }
    if (b4d1_ok) {
        result.pattern = BDIPattern::BASE4_DELTA1;
        result.compressed_size = 20;
        result.data[0] = static_cast<uint8_t>(BDIPattern::BASE4_DELTA1);
        std::memcpy(&result.data[1], &words32[0], 4);
        for (size_t i = 1; i < 16; ++i) {
            result.data[5 + (i - 1)] = static_cast<uint8_t>(static_cast<int8_t>(words32[i] - words32[0]));
        }
        return result;
    }

    // 5. Evaluate Base8-Delta2 (8-byte base + 7x 2-byte delta = 23 bytes)
    bool b8d2_ok = true;
    for (size_t i = 1; i < 8; ++i) {
        int64_t diff = words64[i] - words64[0];
        if (diff < std::numeric_limits<int16_t>::min() || diff > std::numeric_limits<int16_t>::max()) {
            b8d2_ok = false;
            break;
        }
    }
    if (b8d2_ok) {
        result.pattern = BDIPattern::BASE8_DELTA2;
        result.compressed_size = 23;
        result.data[0] = static_cast<uint8_t>(BDIPattern::BASE8_DELTA2);
        std::memcpy(&result.data[1], &words64[0], 8);
        for (size_t i = 1; i < 8; ++i) {
            int16_t delta = static_cast<int16_t>(words64[i] - words64[0]);
            std::memcpy(&result.data[9 + (i - 1) * 2], &delta, 2);
        }
        return result;
    }

    // 6. Evaluate Base2-Delta1 (2-byte base + 31x 1-byte delta = 34 bytes)
    bool b2d1_ok = true;
    for (size_t i = 1; i < 32; ++i) {
        int16_t diff = words16[i] - words16[0];
        if (diff < std::numeric_limits<int8_t>::min() || diff > std::numeric_limits<int8_t>::max()) {
            b2d1_ok = false;
            break;
        }
    }
    if (b2d1_ok) {
        result.pattern = BDIPattern::BASE2_DELTA1;
        result.compressed_size = 34;
        result.data[0] = static_cast<uint8_t>(BDIPattern::BASE2_DELTA1);
        std::memcpy(&result.data[1], &words16[0], 2);
        for (size_t i = 1; i < 32; ++i) {
            result.data[3 + (i - 1)] = static_cast<uint8_t>(static_cast<int8_t>(words16[i] - words16[0]));
        }
        return result;
    }

    // 7. Evaluate Base4-Delta2 (4-byte base + 15x 2-byte delta = 35 bytes)
    bool b4d2_ok = true;
    for (size_t i = 1; i < 16; ++i) {
        int32_t diff = words32[i] - words32[0];
        if (diff < std::numeric_limits<int16_t>::min() || diff > std::numeric_limits<int16_t>::max()) {
            b4d2_ok = false;
            break;
        }
    }
    if (b4d2_ok) {
        result.pattern = BDIPattern::BASE4_DELTA2;
        result.compressed_size = 35;
        result.data[0] = static_cast<uint8_t>(BDIPattern::BASE4_DELTA2);
        std::memcpy(&result.data[1], &words32[0], 4);
        for (size_t i = 1; i < 16; ++i) {
            int16_t delta = static_cast<int16_t>(words32[i] - words32[0]);
            std::memcpy(&result.data[5 + (i - 1) * 2], &delta, 2);
        }
        return result;
    }

    // 8. Evaluate Base8-Delta4 (8-byte base + 7x 4-byte delta = 37 bytes)
    bool b8d4_ok = true;
    for (size_t i = 1; i < 8; ++i) {
        int64_t diff = words64[i] - words64[0];
        if (diff < std::numeric_limits<int32_t>::min() || diff > std::numeric_limits<int32_t>::max()) {
            b8d4_ok = false;
            break;
        }
    }
    if (b8d4_ok) {
        result.pattern = BDIPattern::BASE8_DELTA4;
        result.compressed_size = 37;
        result.data[0] = static_cast<uint8_t>(BDIPattern::BASE8_DELTA4);
        std::memcpy(&result.data[1], &words64[0], 8);
        for (size_t i = 1; i < 8; ++i) {
            int32_t delta = static_cast<int32_t>(words64[i] - words64[0]);
            std::memcpy(&result.data[9 + (i - 1) * 4], &delta, 4);
        }
        return result;
    }

    // 9. Fallback: Uncompressed (64 bytes raw payload)
    result.pattern = BDIPattern::UNCOMPRESSED;
    result.compressed_size = 64;
    result.data[0] = static_cast<uint8_t>(BDIPattern::UNCOMPRESSED);
    std::memcpy(result.data.data(), src_64b, 64);
    return result;
}

bool BDIEngine::Decompress(const CompressedLine& comp, void* dest_64b) noexcept {
    return DecompressRaw(comp.data.data(), comp.compressed_size, dest_64b, comp.pattern);
}

bool BDIEngine::DecompressRaw(const uint8_t* comp_buffer, size_t comp_size, void* dest_64b, std::optional<BDIPattern> explicit_pattern) noexcept {
    if (!comp_buffer || !dest_64b || comp_size == 0) return false;

    BDIPattern pattern = explicit_pattern.value_or(static_cast<BDIPattern>(comp_buffer[0]));

    switch (pattern) {
        case BDIPattern::ZEROS: {
            std::memset(dest_64b, 0, CACHE_LINE_SIZE);
            return true;
        }

        case BDIPattern::REP_WORD: {
            if (comp_size < 9) return false;
            int64_t base_word = 0;
            std::memcpy(&base_word, &comp_buffer[1], 8);
            int64_t* out = static_cast<int64_t*>(dest_64b);
            for (size_t i = 0; i < 8; ++i) {
                out[i] = base_word;
            }
            return true;
        }

        case BDIPattern::BASE8_DELTA1: {
            if (comp_size < 16) return false;
            int64_t base_word = 0;
            std::memcpy(&base_word, &comp_buffer[1], 8);
            int64_t* out = static_cast<int64_t*>(dest_64b);
            out[0] = base_word;
            for (size_t i = 1; i < 8; ++i) {
                int8_t delta = static_cast<int8_t>(comp_buffer[9 + (i - 1)]);
                out[i] = base_word + delta;
            }
            return true;
        }

        case BDIPattern::BASE8_DELTA2: {
            if (comp_size < 23) return false;
            int64_t base_word = 0;
            std::memcpy(&base_word, &comp_buffer[1], 8);
            int64_t* out = static_cast<int64_t*>(dest_64b);
            out[0] = base_word;
            for (size_t i = 1; i < 8; ++i) {
                int16_t delta = 0;
                std::memcpy(&delta, &comp_buffer[9 + (i - 1) * 2], 2);
                out[i] = base_word + delta;
            }
            return true;
        }

        case BDIPattern::BASE8_DELTA4: {
            if (comp_size < 37) return false;
            int64_t base_word = 0;
            std::memcpy(&base_word, &comp_buffer[1], 8);
            int64_t* out = static_cast<int64_t*>(dest_64b);
            out[0] = base_word;
            for (size_t i = 1; i < 8; ++i) {
                int32_t delta = 0;
                std::memcpy(&delta, &comp_buffer[9 + (i - 1) * 4], 4);
                out[i] = base_word + delta;
            }
            return true;
        }

        case BDIPattern::BASE4_DELTA1: {
            if (comp_size < 20) return false;
            int32_t base_word = 0;
            std::memcpy(&base_word, &comp_buffer[1], 4);
            int32_t* out = static_cast<int32_t*>(dest_64b);
            out[0] = base_word;
            for (size_t i = 1; i < 16; ++i) {
                int8_t delta = static_cast<int8_t>(comp_buffer[5 + (i - 1)]);
                out[i] = base_word + delta;
            }
            return true;
        }

        case BDIPattern::BASE4_DELTA2: {
            if (comp_size < 35) return false;
            int32_t base_word = 0;
            std::memcpy(&base_word, &comp_buffer[1], 4);
            int32_t* out = static_cast<int32_t*>(dest_64b);
            out[0] = base_word;
            for (size_t i = 1; i < 16; ++i) {
                int16_t delta = 0;
                std::memcpy(&delta, &comp_buffer[5 + (i - 1) * 2], 2);
                out[i] = base_word + delta;
            }
            return true;
        }

        case BDIPattern::BASE2_DELTA1: {
            if (comp_size < 34) return false;
            int16_t base_word = 0;
            std::memcpy(&base_word, &comp_buffer[1], 2);
            int16_t* out = static_cast<int16_t*>(dest_64b);
            out[0] = base_word;
            for (size_t i = 1; i < 32; ++i) {
                int8_t delta = static_cast<int8_t>(comp_buffer[3 + (i - 1)]);
                out[i] = base_word + delta;
            }
            return true;
        }

        case BDIPattern::UNCOMPRESSED: {
            // Uncompressed line is stored as 64 raw bytes directly
            std::memcpy(dest_64b, comp_buffer, CACHE_LINE_SIZE);
            return true;
        }

        default:
            return false;
    }
}

} // namespace hyper_ram
