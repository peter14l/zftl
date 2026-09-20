// Copyright (c) 2026 zFTL Project. All Rights Reserved.
// zFTL Verification Test Suite — LZ4Compressor, FlashModel, FTLController.
#include "lz4_compressor.hpp"
#include "flash_model.hpp"
#include "ftl_controller.hpp"
#include <iostream>
#include <vector>
#include <random>
#include <cstring>
#include <cassert>
#include <array>
#include <string>

using namespace zftl;

static int g_tests_passed = 0;
static int g_tests_failed = 0;

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[-] FAILED: " << msg << " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
            g_tests_failed++; \
            return false; \
        } \
    } while (0)

#define RUN_TEST(fn) \
    do { \
        std::cout << "[*] Running " << #fn << "...\n"; \
        if (fn()) { \
            std::cout << "[+] PASSED: " << #fn << "\n"; \
            g_tests_passed++; \
        } else { \
            std::cout << "[-] FAILED: " << #fn << "\n"; \
        } \
    } while (0)

// ============================================================
// Section 1: LZ4Compressor Unit Tests
// ============================================================

// 1.1 All-zero 4 KB block must be identified as sparse-zero (no flash write)
bool TestLZ4_ZeroBlockDetected() {
    alignas(64) std::array<uint8_t, FLASH_PAGE_SIZE> src{};
    src.fill(0);
    CompressedBlock comp = LZ4Compressor::Compress(src.data());
    TEST_ASSERT(comp.is_zero_block, "Zero block must be flagged as sparse-zero");
    TEST_ASSERT(comp.compressed_size == 0, "Sparse-zero must have 0 compressed bytes");
    TEST_ASSERT(comp.slot == SlotAllocation::SPARSE_ZERO, "Slot must be SPARSE_ZERO");
    return true;
}

// 1.2 Zero block decompresses back to all-zeros (lossless round-trip)
bool TestLZ4_ZeroRoundTrip() {
    alignas(64) std::array<uint8_t, FLASH_PAGE_SIZE> src{};
    alignas(64) std::array<uint8_t, FLASH_PAGE_SIZE> dest{};
    dest.fill(0xAA);

    CompressedBlock comp = LZ4Compressor::Compress(src.data());
    bool ok = LZ4Compressor::Decompress(comp, dest.data());
    TEST_ASSERT(ok, "Decompression of zero block must succeed");
    TEST_ASSERT(std::memcmp(src.data(), dest.data(), FLASH_PAGE_SIZE) == 0,
                "Zero block round-trip data mismatch");
    return true;
}

// 1.3 JSON / text block compresses below 2 KB (half-page slot)
bool TestLZ4_JsonCompressesToHalfPage() {
    alignas(64) std::array<uint8_t, FLASH_PAGE_SIZE> src{};
    const std::string json = R"({"ts":"2026-09-18T08:00:00Z","type":"cache","url":"https://cdn.example.com/app.js","headers":{"content-type":"application/javascript","cache-control":"max-age=31536000"}})";
    for (size_t i = 0; i < FLASH_PAGE_SIZE; ++i) src[i] = static_cast<uint8_t>(json[i % json.size()]);

    CompressedBlock comp = LZ4Compressor::Compress(src.data());
    TEST_ASSERT(comp.is_compressed, "JSON text must compress");
    TEST_ASSERT(comp.compressed_size <= 2048, "JSON must fit in a half-page slot (<= 2 KB)");
    TEST_ASSERT(comp.slot == SlotAllocation::SLOT_1KB || comp.slot == SlotAllocation::SLOT_2KB,
                "JSON slot must be SLOT_1KB or SLOT_2KB");

    // Round-trip
    alignas(64) std::array<uint8_t, FLASH_PAGE_SIZE> dest{};
    bool ok = LZ4Compressor::Decompress(comp, dest.data());
    TEST_ASSERT(ok, "JSON decompression must succeed");
    TEST_ASSERT(std::memcmp(src.data(), dest.data(), FLASH_PAGE_SIZE) == 0,
                "JSON round-trip data mismatch");
    return true;
}

// 1.4 Binary code (repeated 4-byte x86 instruction pattern) compresses well
bool TestLZ4_BinaryCodeRoundTrip() {
    alignas(64) std::array<uint8_t, FLASH_PAGE_SIZE> src{};
    for (size_t b = 0; b < FLASH_PAGE_SIZE; b += 4) {
        src[b]=0x48; src[b+1]=0x89; src[b+2]=0x5C; src[b+3]=0x07;
    }
    CompressedBlock comp = LZ4Compressor::Compress(src.data());
    TEST_ASSERT(comp.is_compressed, "Repetitive binary code must compress");

    alignas(64) std::array<uint8_t, FLASH_PAGE_SIZE> dest{};
    bool ok = LZ4Compressor::Decompress(comp, dest.data());
    TEST_ASSERT(ok, "Binary code decompression must succeed");
    TEST_ASSERT(std::memcmp(src.data(), dest.data(), FLASH_PAGE_SIZE) == 0,
                "Binary code round-trip data mismatch");
    return true;
}

// 1.5 High-entropy random noise must fall back to FULL_4KB (incompressible)
bool TestLZ4_RandomNoiseIncompressible() {
    alignas(64) std::array<uint8_t, FLASH_PAGE_SIZE> src{};
    std::mt19937_64 rng(0xDEADBEEF42ULL);
    for (size_t b = 0; b < FLASH_PAGE_SIZE; b += 8) {
        uint64_t v = rng();
        std::memcpy(src.data() + b, &v, 8);
    }
    CompressedBlock comp = LZ4Compressor::Compress(src.data());
    // Incompressible noise: LZ4 expands it, so we must store raw
    TEST_ASSERT(!comp.is_compressed || comp.compressed_size >= 3072,
                "Random noise must result in full-page fallback");

    // Round-trip must still be lossless
    alignas(64) std::array<uint8_t, FLASH_PAGE_SIZE> dest{};
    bool ok = LZ4Compressor::Decompress(comp, dest.data());
    TEST_ASSERT(ok, "Incompressible block decompression must succeed");
    TEST_ASSERT(std::memcmp(src.data(), dest.data(), FLASH_PAGE_SIZE) == 0,
                "Incompressible block round-trip data mismatch");
    return true;
}

// 1.6 QuantizeSlot: boundary conditions
bool TestLZ4_QuantizeSlot() {
    TEST_ASSERT(LZ4Compressor::QuantizeSlot(0, true)    == SlotAllocation::SPARSE_ZERO, "0-byte zero must be SPARSE_ZERO");
    TEST_ASSERT(LZ4Compressor::QuantizeSlot(1, false)   == SlotAllocation::SLOT_1KB,   "1 byte must be SLOT_1KB");
    TEST_ASSERT(LZ4Compressor::QuantizeSlot(1024, false)== SlotAllocation::SLOT_1KB,   "1024 bytes must be SLOT_1KB");
    TEST_ASSERT(LZ4Compressor::QuantizeSlot(1025, false)== SlotAllocation::SLOT_2KB,   "1025 bytes must be SLOT_2KB");
    TEST_ASSERT(LZ4Compressor::QuantizeSlot(2048, false)== SlotAllocation::SLOT_2KB,   "2048 bytes must be SLOT_2KB");
    TEST_ASSERT(LZ4Compressor::QuantizeSlot(2049, false)== SlotAllocation::SLOT_3KB,   "2049 bytes must be SLOT_3KB");
    TEST_ASSERT(LZ4Compressor::QuantizeSlot(4096, false)== SlotAllocation::FULL_4KB,   "4096 bytes must be FULL_4KB");
    return true;
}

// ============================================================
// Section 2: FlashModel Unit Tests
// ============================================================

static FlashConfig MakeTestConfig() {
    FlashConfig cfg;
    cfg.page_size_bytes  = 4096;
    cfg.pages_per_block  = 16; // Smaller for faster tests
    cfg.total_blocks     = 32;
    cfg.max_pe_cycles    = 500;
    cfg.prog_latency_us  = 0.0; // Remove latency overhead for unit tests
    cfg.read_latency_us  = 0.0;
    cfg.erase_latency_us = 0.0;
    return cfg;
}

// 2.1 ProgramPage stores data and marks page as programmed
bool TestFlash_ProgramAndRead() {
    FlashModel flash(MakeTestConfig());
    alignas(64) std::array<uint8_t, 4096> src{};
    src.fill(0xBE);

    bool ok = flash.ProgramPage(0, 0, src.data());
    TEST_ASSERT(ok, "ProgramPage must succeed on erased page");
    TEST_ASSERT(flash.IsPageProgrammed(0, 0), "Page must be marked programmed after write");
    TEST_ASSERT(flash.IsPageValid(0, 0), "Page must be valid after program");

    alignas(64) std::array<uint8_t, 4096> dest{};
    flash.ReadPage(0, 0, dest.data());
    TEST_ASSERT(std::memcmp(src.data(), dest.data(), 4096) == 0, "Read-back data mismatch");
    return true;
}

// 2.2 NAND overwrite constraint: cannot program a page without erase
bool TestFlash_NoOverwriteWithoutErase() {
    FlashModel flash(MakeTestConfig());
    alignas(64) std::array<uint8_t, 4096> src{};

    flash.ProgramPage(0, 0, src.data());
    bool second = flash.ProgramPage(0, 0, src.data()); // Must fail
    TEST_ASSERT(!second, "ProgramPage on already-programmed page must fail (NAND constraint)");
    return true;
}

// 2.3 EraseBlock resets page state so it can be programmed again
bool TestFlash_EraseResetsBlock() {
    FlashModel flash(MakeTestConfig());
    alignas(64) std::array<uint8_t, 4096> src{};
    src.fill(0xAB);

    flash.ProgramPage(0, 0, src.data());
    TEST_ASSERT(flash.IsPageProgrammed(0, 0), "Page should be programmed");
    flash.EraseBlock(0);
    TEST_ASSERT(!flash.IsPageProgrammed(0, 0), "Page must be erased after EraseBlock");
    TEST_ASSERT(flash.GetEraseCount(0) == 1, "Erase count must increment to 1");

    bool ok = flash.ProgramPage(0, 0, src.data()); // Must succeed post-erase
    TEST_ASSERT(ok, "ProgramPage after erase must succeed");
    return true;
}

// 2.4 InvalidatePage marks page as stale without erasing
bool TestFlash_InvalidatePage() {
    FlashModel flash(MakeTestConfig());
    alignas(64) std::array<uint8_t, 4096> src{};
    flash.ProgramPage(0, 0, src.data());

    TEST_ASSERT(flash.IsPageValid(0, 0), "Page must be valid before invalidation");
    flash.InvalidatePage(0, 0);
    TEST_ASSERT(!flash.IsPageValid(0, 0), "Page must be invalid after InvalidatePage");
    TEST_ASSERT(flash.GetInvalidCount(0) == 1, "Invalid count must be 1");
    TEST_ASSERT(flash.IsPageProgrammed(0, 0), "Physical bits still present — page is programmed but stale");
    return true;
}

// 2.5 Telemetry accurately counts programmed pages and erased blocks
bool TestFlash_Telemetry() {
    FlashModel flash(MakeTestConfig());
    alignas(64) std::array<uint8_t, 4096> src{};

    flash.ProgramPage(0, 0, src.data());
    flash.ProgramPage(0, 1, src.data());
    flash.EraseBlock(0);

    FlashTelemetry tel = flash.GetTelemetry();
    TEST_ASSERT(tel.total_pages_programmed == 2, "Telemetry must count 2 programmed pages");
    TEST_ASSERT(tel.total_blocks_erased == 1, "Telemetry must count 1 erased block");
    TEST_ASSERT(tel.total_physical_bytes_written == 2 * 4096,
                "Physical bytes written must be 2 * page_size");
    return true;
}

// ============================================================
// Section 3: FTLController Integration Tests
// ============================================================

static FTLController MakeTestFTL() {
    FlashConfig cfg;
    cfg.page_size_bytes  = 4096;
    cfg.pages_per_block  = 16;
    cfg.total_blocks     = 64;
    cfg.prog_latency_us  = 0.0;
    cfg.read_latency_us  = 0.0;
    cfg.erase_latency_us = 0.0;
    return FTLController(64 * 4096, cfg); // 64 LBAs
}

// 3.1 Write then read back all-zero block (sparse-zero path — no flash write)
bool TestFTL_SparseZeroBypass() {
    auto ftl = MakeTestFTL();
    alignas(64) std::array<uint8_t, 4096> src{};
    src.fill(0);

    bool ok = ftl.WriteBlock(0, src.data());
    TEST_ASSERT(ok, "WriteBlock of zero must succeed");

    FTLTelemetry tel = ftl.GetTelemetry();
    TEST_ASSERT(tel.zero_blocks_filtered == 1, "Zero block must be counted in zero_blocks_filtered");
    TEST_ASSERT(tel.flash_tel.total_pages_programmed == 0,
                "Sparse-zero must not touch physical flash");

    alignas(64) std::array<uint8_t, 4096> dest{};
    dest.fill(0xFF);
    ok = ftl.ReadBlock(0, dest.data());
    TEST_ASSERT(ok, "ReadBlock of sparse-zero must succeed");
    TEST_ASSERT(std::memcmp(src.data(), dest.data(), 4096) == 0, "Sparse-zero read-back mismatch");
    return true;
}

// 3.2 Two compressible blocks get packed into a single physical flash page
bool TestFTL_HalfPagePacking() {
    auto ftl = MakeTestFTL();
    alignas(64) std::array<uint8_t, 4096> src{};

    // Fill with compressible JSON-like repeating text
    const std::string json = R"({"key":"value","repeated":"AAAAAAAAAAAAAAAAAAAAA"})";
    for (size_t b = 0; b < 4096; ++b) src[b] = static_cast<uint8_t>(json[b % json.size()]);

    ftl.WriteBlock(0, src.data());
    ftl.WriteBlock(1, src.data());
    ftl.FlushStagedWrites();

    FTLTelemetry tel = ftl.GetTelemetry();
    TEST_ASSERT(tel.compressed_half_pages_packed >= 1,
                "Two compressible blocks must produce at least one half-page pack");
    TEST_ASSERT(tel.flash_tel.total_pages_programmed == 1,
                "Two half-pages must land in exactly one physical flash page");

    // Verify both LBAs read back correctly
    alignas(64) std::array<uint8_t, 4096> dest{};
    bool ok0 = ftl.ReadBlock(0, dest.data());
    TEST_ASSERT(ok0, "ReadBlock LBA 0 must succeed");
    TEST_ASSERT(std::memcmp(src.data(), dest.data(), 4096) == 0, "LBA 0 read-back mismatch");

    bool ok1 = ftl.ReadBlock(1, dest.data());
    TEST_ASSERT(ok1, "ReadBlock LBA 1 must succeed");
    TEST_ASSERT(std::memcmp(src.data(), dest.data(), 4096) == 0, "LBA 1 read-back mismatch");
    return true;
}

// 3.3 Incompressible block written as raw full 4 KB page
bool TestFTL_RawFullPageFallback() {
    auto ftl = MakeTestFTL();
    alignas(64) std::array<uint8_t, 4096> src{};
    std::mt19937_64 rng(0xCAFE);
    for (size_t b = 0; b < 4096; b += 8) { uint64_t v = rng(); std::memcpy(src.data()+b, &v, 8); }

    ftl.WriteBlock(0, src.data());

    FTLTelemetry tel = ftl.GetTelemetry();
    TEST_ASSERT(tel.raw_full_pages_written == 1, "Incompressible block must count as raw_full_pages_written");
    TEST_ASSERT(tel.flash_tel.total_pages_programmed == 1, "Incompressible block must occupy one full flash page");

    alignas(64) std::array<uint8_t, 4096> dest{};
    bool ok = ftl.ReadBlock(0, dest.data());
    TEST_ASSERT(ok, "ReadBlock of raw page must succeed");
    TEST_ASSERT(std::memcmp(src.data(), dest.data(), 4096) == 0, "Raw full page round-trip mismatch");
    return true;
}

// 3.4 Overwrite: writing to same LBA twice invalidates the old physical page
bool TestFTL_OverwriteInvalidatesOldPage() {
    auto ftl = MakeTestFTL();
    alignas(64) std::array<uint8_t, 4096> src1{}, src2{};
    std::mt19937_64 rng(1);
    for (size_t b = 0; b < 4096; b += 8) { uint64_t v = rng(); std::memcpy(src1.data()+b, &v, 8); }
    for (size_t b = 0; b < 4096; b += 8) { uint64_t v = rng(); std::memcpy(src2.data()+b, &v, 8); }

    ftl.WriteBlock(5, src1.data()); // First write
    ftl.WriteBlock(5, src2.data()); // Overwrite

    alignas(64) std::array<uint8_t, 4096> dest{};
    bool ok = ftl.ReadBlock(5, dest.data());
    TEST_ASSERT(ok, "ReadBlock after overwrite must succeed");
    TEST_ASSERT(std::memcmp(src2.data(), dest.data(), 4096) == 0,
                "Overwrite must return latest data, not stale data");
    return true;
}

// 3.5 Unmapped LBA reads back as zeros (not garbage)
bool TestFTL_UnmappedLBAReadsZero() {
    auto ftl = MakeTestFTL();
    alignas(64) std::array<uint8_t, 4096> dest{};
    dest.fill(0xFF);
    bool ok = ftl.ReadBlock(10, dest.data());
    TEST_ASSERT(ok, "Read from unmapped LBA must not fail");
    for (size_t i = 0; i < 4096; ++i)
        TEST_ASSERT(dest[i] == 0, "Unmapped LBA must return zeros");
    return true;
}

// 3.6 WAF < 1.0 on all-compressible workload
bool TestFTL_WAF_CompressibleWorkload() {
    FlashConfig cfg;
    cfg.page_size_bytes  = 4096;
    cfg.pages_per_block  = 64;
    cfg.total_blocks     = 256;
    cfg.prog_latency_us  = 0.0;
    cfg.read_latency_us  = 0.0;
    cfg.erase_latency_us = 0.0;
    FTLController ftl(64 * 1024 * 1024, cfg);

    alignas(64) std::array<uint8_t, 4096> buf{};
    const std::string payload = R"({"event":"page_write","lba":0,"compressible":true,"padding":"XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX"})";
    for (size_t i = 0; i < 2048; ++i) {
        for (size_t b = 0; b < 4096; ++b) buf[b] = static_cast<uint8_t>(payload[(b + i) % payload.size()]);
        ftl.WriteBlock(i, buf.data());
    }
    ftl.FlushStagedWrites();

    FTLTelemetry tel = ftl.GetTelemetry();
    TEST_ASSERT(tel.write_amplification_factor > 0.0, "WAF must be positive");
    TEST_ASSERT(tel.write_amplification_factor < 1.0,
                "WAF must be < 1.0 on compressible workload (half-page packing active)");
    return true;
}

// 3.7 WAF = 1.0 on incompressible workload (graceful fallback, no corruption)
bool TestFTL_WAF_IncompressibleGracefulFallback() {
    FlashConfig cfg;
    cfg.page_size_bytes  = 4096;
    cfg.pages_per_block  = 64;
    cfg.total_blocks     = 256;
    cfg.prog_latency_us  = 0.0;
    cfg.read_latency_us  = 0.0;
    cfg.erase_latency_us = 0.0;
    FTLController ftl(64 * 1024 * 1024, cfg);

    alignas(64) std::array<uint8_t, 4096> buf{};
    std::mt19937_64 rng(0xBEEFCAFE);
    for (size_t i = 0; i < 512; ++i) {
        for (size_t b = 0; b < 4096; b += 8) { uint64_t v = rng(); std::memcpy(buf.data()+b, &v, 8); }
        ftl.WriteBlock(i, buf.data());
    }
    ftl.FlushStagedWrites();

    FTLTelemetry tel = ftl.GetTelemetry();
    // WAF on incompressible data should be ~1.0 (no benefit but no corruption)
    TEST_ASSERT(tel.write_amplification_factor >= 0.9 && tel.write_amplification_factor <= 1.1,
                "WAF on incompressible workload must be approximately 1.0");
    return true;
}

// 3.8 Garbage collection is triggered and migrates pages without data loss
bool TestFTL_GC_DataIntegrityAfterGC() {
    // Small flash to force GC quickly
    FlashConfig cfg;
    cfg.page_size_bytes  = 4096;
    cfg.pages_per_block  = 8;   // Only 8 pages per block
    cfg.total_blocks     = 16;  // 16 blocks = 128 pages total
    cfg.prog_latency_us  = 0.0;
    cfg.read_latency_us  = 0.0;
    cfg.erase_latency_us = 0.0;
    FTLController ftl(32 * 4096, cfg); // 32 LBAs

    // Use incompressible data so every block takes exactly one page
    std::mt19937_64 rng(0xABCDEF);
    alignas(64) std::array<uint8_t, 4096> bufs[32];
    for (int i = 0; i < 32; ++i) {
        for (size_t b = 0; b < 4096; b += 8) {
            uint64_t v = rng();
            std::memcpy(bufs[i].data() + b, &v, 8);
        }
    }

    // Write all LBAs — this will exhaust physical space and trigger GC
    for (int i = 0; i < 32; ++i) ftl.WriteBlock(i, bufs[i].data());
    // Overwrite to create lots of invalid pages and force real GC
    for (int i = 0; i < 32; ++i) ftl.WriteBlock(i, bufs[i].data());
    ftl.FlushStagedWrites();

    FTLTelemetry tel = ftl.GetTelemetry();
    TEST_ASSERT(tel.gc_invocations > 0, "GC must be triggered during space exhaustion");
    TEST_ASSERT(tel.gc_pages_migrated >= 0, "GC migration count must be non-negative");

    // Verify data integrity of all LBAs post-GC
    alignas(64) std::array<uint8_t, 4096> dest{};
    for (int i = 0; i < 32; ++i) {
        bool ok = ftl.ReadBlock(i, dest.data());
        TEST_ASSERT(ok, "ReadBlock must succeed after GC");
        TEST_ASSERT(std::memcmp(bufs[i].data(), dest.data(), 4096) == 0,
                    "Data integrity must be preserved across GC migrations");
    }
    return true;
}

// ============================================================
// Section 4: End-to-End WAF Regression Check
// ============================================================

// 4.1 Realistic blended workload: WAF must stay below 0.8 (matching PROGRESS.md claim)
bool TestFTL_E2E_BlendedWorkloadWAF() {
    FlashConfig cfg;
    cfg.page_size_bytes  = 4096;
    cfg.pages_per_block  = 64;
    cfg.total_blocks     = 256;
    cfg.prog_latency_us  = 0.0;
    cfg.read_latency_us  = 0.0;
    cfg.erase_latency_us = 0.0;
    FTLController ftl(64 * 1024 * 1024, cfg);

    alignas(64) std::array<uint8_t, 4096> buf{};
    std::mt19937_64 rng(99);
    const std::string json = R"({"ts":"2026-09-18T08:00:00Z","event":"write","payload":"AAAAABBBBBBBBBBCCCCCCCCCCCCCCCC"})";

    for (size_t i = 0; i < 4096; ++i) {
        size_t mod = i % 20;
        if (mod < 7) {
            std::memset(buf.data(), 0, 4096); // 35% zeros
        } else if (mod < 15) {
            for (size_t b = 0; b < 4096; ++b) buf[b] = static_cast<uint8_t>(json[(b+i) % json.size()]);
        } else if (mod < 18) {
            for (size_t b = 0; b < 4096; b += 4) { buf[b]=0x48; buf[b+1]=0x89; buf[b+2]=0x5C; buf[b+3]=0x07; }
        } else {
            for (size_t b = 0; b < 4096; b += 8) { uint64_t v = rng(); std::memcpy(buf.data()+b, &v, 8); }
        }
        ftl.WriteBlock(i % (64*256), buf.data());
    }
    ftl.FlushStagedWrites();

    FTLTelemetry tel = ftl.GetTelemetry();
    std::cout << "    [E2E] Blended WAF = " << tel.write_amplification_factor
              << " | Zero pages: " << tel.zero_blocks_filtered
              << " | Half-page packs: " << tel.compressed_half_pages_packed << "\n";
    TEST_ASSERT(tel.write_amplification_factor < 0.8,
                "Blended real-world WAF must remain below 0.8");
    return true;
}

// ============================================================
// Main
// ============================================================
int main() {
    std::cout << "\033[1;36m";
    std::cout << "=====================================================\n";
    std::cout << "  zFTL Verification Test Suite\n";
    std::cout << "  LZ4Compressor | FlashModel | FTLController\n";
    std::cout << "=====================================================\n";
    std::cout << "\033[0m\n";

    std::cout << "\033[1;33m--- Section 1: LZ4Compressor ---\033[0m\n";
    RUN_TEST(TestLZ4_ZeroBlockDetected);
    RUN_TEST(TestLZ4_ZeroRoundTrip);
    RUN_TEST(TestLZ4_JsonCompressesToHalfPage);
    RUN_TEST(TestLZ4_BinaryCodeRoundTrip);
    RUN_TEST(TestLZ4_RandomNoiseIncompressible);
    RUN_TEST(TestLZ4_QuantizeSlot);

    std::cout << "\n\033[1;33m--- Section 2: FlashModel ---\033[0m\n";
    RUN_TEST(TestFlash_ProgramAndRead);
    RUN_TEST(TestFlash_NoOverwriteWithoutErase);
    RUN_TEST(TestFlash_EraseResetsBlock);
    RUN_TEST(TestFlash_InvalidatePage);
    RUN_TEST(TestFlash_Telemetry);

    std::cout << "\n\033[1;33m--- Section 3: FTLController ---\033[0m\n";
    RUN_TEST(TestFTL_SparseZeroBypass);
    RUN_TEST(TestFTL_HalfPagePacking);
    RUN_TEST(TestFTL_RawFullPageFallback);
    RUN_TEST(TestFTL_OverwriteInvalidatesOldPage);
    RUN_TEST(TestFTL_UnmappedLBAReadsZero);
    RUN_TEST(TestFTL_WAF_CompressibleWorkload);
    RUN_TEST(TestFTL_WAF_IncompressibleGracefulFallback);
    RUN_TEST(TestFTL_GC_DataIntegrityAfterGC);

    std::cout << "\n\033[1;33m--- Section 4: End-to-End Regression ---\033[0m\n";
    RUN_TEST(TestFTL_E2E_BlendedWorkloadWAF);

    int total = g_tests_passed + g_tests_failed;
    std::cout << "\n\033[1;36m=====================================================\n";
    if (g_tests_failed == 0) {
        std::cout << "  ALL " << total << "/" << total << " TESTS PASSED ✓\n";
    } else {
        std::cout << "  " << g_tests_passed << "/" << total << " PASSED | "
                  << g_tests_failed << " FAILED ✗\n";
    }
    std::cout << "=====================================================\033[0m\n\n";

    return (g_tests_failed == 0) ? 0 : 1;
}
