// Copyright (c) 2026 HyperRAM Project. All Rights Reserved.
// Open-Hardware / Low-Cost Memory Architecture Initiative.
#include "bdi_engine.hpp"
#include "line_table.hpp"
#include "hyper_ram_controller.hpp"
#include <iostream>
#include <vector>
#include <random>
#include <cstring>
#include <cassert>

using namespace hyper_ram;

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

bool TestZerosLossless() {
    alignas(64) std::array<uint8_t, 64> src{};
    CompressedLine comp = BDIEngine::Compress(src.data());
    TEST_ASSERT(comp.pattern == BDIPattern::ZEROS, "Pattern must be ZEROS");
    TEST_ASSERT(comp.compressed_size == 1, "Zeros size must be 1 byte tag");

    alignas(64) std::array<uint8_t, 64> dest{};
    dest.fill(0xAA);
    bool ok = BDIEngine::Decompress(comp, dest.data());
    TEST_ASSERT(ok, "Decompression failed");
    TEST_ASSERT(std::memcmp(src.data(), dest.data(), 64) == 0, "Data mismatch on zeros");
    return true;
}

bool TestRepeatedWordLossless() {
    alignas(64) std::array<uint64_t, 8> src{};
    src.fill(0xDEADBEEFCAFEBABEULL);

    CompressedLine comp = BDIEngine::Compress(src.data());
    TEST_ASSERT(comp.pattern == BDIPattern::REP_WORD, "Pattern must be REP_WORD");
    TEST_ASSERT(comp.compressed_size == 9, "Repeated word size must be 9 bytes");

    alignas(64) std::array<uint64_t, 8> dest{};
    bool ok = BDIEngine::Decompress(comp, dest.data());
    TEST_ASSERT(ok, "Decompression failed");
    TEST_ASSERT(std::memcmp(src.data(), dest.data(), 64) == 0, "Data mismatch on repeated word");
    return true;
}

bool TestBase8Delta1Lossless() {
    // Array of 64-bit pointers with small variations (e.g. heap pointers: 0x7FFF0000 + i*8)
    alignas(64) std::array<int64_t, 8> src{};
    int64_t base = 0x7FFF'0000'1000LL;
    for (size_t i = 0; i < 8; ++i) {
        src[i] = base + static_cast<int64_t>(i * 12);
    }

    CompressedLine comp = BDIEngine::Compress(src.data());
    TEST_ASSERT(comp.pattern == BDIPattern::BASE8_DELTA1, "Pattern must be BASE8_DELTA1");
    TEST_ASSERT(comp.compressed_size == 16, "Base8-Delta1 size must be 16 bytes");

    alignas(64) std::array<int64_t, 8> dest{};
    bool ok = BDIEngine::Decompress(comp, dest.data());
    TEST_ASSERT(ok, "Decompression failed");
    TEST_ASSERT(std::memcmp(src.data(), dest.data(), 64) == 0, "Data mismatch on Base8-Delta1");
    return true;
}

bool TestBase4Delta1Lossless() {
    // Array of 32-bit integers with small deltas
    alignas(64) std::array<int32_t, 16> src{};
    int32_t base = 500000;
    for (size_t i = 0; i < 16; ++i) {
        src[i] = base + static_cast<int32_t>(i * 3 - 20);
    }

    CompressedLine comp = BDIEngine::Compress(src.data());
    TEST_ASSERT(comp.pattern == BDIPattern::BASE4_DELTA1, "Pattern must be BASE4_DELTA1");
    TEST_ASSERT(comp.compressed_size == 20, "Base4-Delta1 size must be 20 bytes");

    alignas(64) std::array<int32_t, 16> dest{};
    bool ok = BDIEngine::Decompress(comp, dest.data());
    TEST_ASSERT(ok, "Decompression failed");
    TEST_ASSERT(std::memcmp(src.data(), dest.data(), 64) == 0, "Data mismatch on Base4-Delta1");
    return true;
}

bool TestIncompressibleFallback() {
    // High-entropy random data
    alignas(64) std::array<uint8_t, 64> src{};
    std::mt19937 rng(1337);
    for (auto& b : src) b = static_cast<uint8_t>(rng() & 0xFF);

    CompressedLine comp = BDIEngine::Compress(src.data());
    TEST_ASSERT(comp.pattern == BDIPattern::UNCOMPRESSED, "Must tag random noise as UNCOMPRESSED");
    TEST_ASSERT(comp.compressed_size == 64, "Uncompressed size must be 64");

    alignas(64) std::array<uint8_t, 64> dest{};
    bool ok = BDIEngine::Decompress(comp, dest.data());
    TEST_ASSERT(ok, "Decompression failed");
    TEST_ASSERT(std::memcmp(src.data(), dest.data(), 64) == 0, "Data mismatch on uncompressed fallback");
    return true;
}

bool TestLineTableAllocationAndCompaction() {
    // Virtual 1MB, Physical 512KB
    size_t virt = 1024 * 1024;
    size_t phys = 512 * 1024;
    MemoryLineTable table(virt, phys);

    alignas(64) std::array<uint64_t, 8> line{};
    for (size_t l = 0; l < 1000; ++l) {
        line.fill(0x1000 + l); // Repeated word (compresses to 9 bytes, slots to 16 bytes)
        CompressedLine comp = BDIEngine::Compress(line.data());
        bool stored = table.StoreLine(l, comp);
        TEST_ASSERT(stored, "Failed to store line in MemoryLineTable");
    }

    // Verify all lines read back correctly
    for (size_t l = 0; l < 1000; ++l) {
        alignas(64) std::array<uint64_t, 8> readback{};
        bool loaded = table.LoadLine(l, readback.data());
        TEST_ASSERT(loaded, "Failed to load line from MemoryLineTable");
        for (size_t i = 0; i < 8; ++i) {
            TEST_ASSERT(readback[i] == 0x1000 + l, "Line data mismatch");
        }
    }

    double ratio = table.GetCurrentCompressionRatio();
    TEST_ASSERT(ratio >= 3.5, "Compression ratio should be >= 3.5x for repeated words");
    return true;
}

bool TestHyperRAMControllerByteAddressable() {
    ControllerConfig cfg;
    cfg.virtual_capacity_bytes = 4 * 1024 * 1024; // 4MB
    cfg.physical_dram_bytes = 2 * 1024 * 1024;    // 2MB (2:1 physical ratio)
    HyperRAMController ctrl(cfg);

    // Write a string spanning across multiple cache lines with unaligned offsets
    const char* message = "HyperRAM: Ultra-low-latency hardware-compressed memory controller!";
    size_t len = std::strlen(message) + 1;
    uint64_t offset = 123; // Unaligned

    bool written = ctrl.Write(offset, message, len);
    TEST_ASSERT(written, "Unaligned write failed");

    std::vector<char> buffer(len, 0);
    bool read = ctrl.Read(offset, buffer.data(), len);
    TEST_ASSERT(read, "Unaligned read failed");
    TEST_ASSERT(std::strcmp(message, buffer.data()) == 0, "Readback string does not match written string");

    return true;
}

bool TestVirtualCapacityDoublerStress() {
    // 8MB virtual memory packed into 4MB physical memory
    ControllerConfig cfg;
    cfg.virtual_capacity_bytes = 8 * 1024 * 1024;
    cfg.physical_dram_bytes = 4 * 1024 * 1024;
    HyperRAMController ctrl(cfg);

    // Populate 6MB of virtual memory (exceeding 4MB physical capacity!)
    std::vector<int64_t> test_data(1024 * 1024); // 8MB of pointers
    for (size_t i = 0; i < test_data.size(); ++i) {
        test_data[i] = 0x7FFF0000 + (i % 8); // Compresses efficiently
    }

    size_t write_bytes = 6 * 1024 * 1024; // 6MB
    bool ok = ctrl.Write(0, test_data.data(), write_bytes);
    TEST_ASSERT(ok, "Failed to write 6MB into 4MB physical pool with compression!");

    MemoryTelemetry tel = ctrl.GetTelemetry();
    TEST_ASSERT(tel.compression_ratio > 1.5, "Compression ratio should be > 1.5x");
    TEST_ASSERT(tel.physical_bytes_stored <= cfg.physical_dram_bytes, "Physical storage must not exceed capacity");

    // Read back and verify
    std::vector<int64_t> read_data(write_bytes / sizeof(int64_t));
    ok = ctrl.Read(0, read_data.data(), write_bytes);
    TEST_ASSERT(ok, "Failed to read back 6MB");
    TEST_ASSERT(std::memcmp(test_data.data(), read_data.data(), write_bytes) == 0, "6MB stress data mismatch");

    return true;
}

#include "lz4_compressor.hpp"
#include <chrono>

using namespace zftl;

bool TestLZ4ZeroBlockLossless() {
    alignas(64) std::array<uint8_t, FLASH_PAGE_SIZE> src{};
    CompressedBlock comp = LZ4Compressor::Compress(src.data());
    TEST_ASSERT(comp.is_zero_block, "Must be flagged as zero block");
    TEST_ASSERT(comp.slot == SlotAllocation::SPARSE_ZERO, "Must allocate SPARSE_ZERO slot");
    TEST_ASSERT(comp.compressed_size == 0, "Compressed size must be 0 for zero block");

    alignas(64) std::array<uint8_t, FLASH_PAGE_SIZE> dest{};
    dest.fill(0xFF);
    bool ok = LZ4Compressor::Decompress(comp, dest.data());
    TEST_ASSERT(ok, "Decompression failed on zero block");
    TEST_ASSERT(std::memcmp(src.data(), dest.data(), FLASH_PAGE_SIZE) == 0, "Data mismatch on zero block");
    return true;
}

bool TestLZ4TextJsonLossless() {
    alignas(64) std::array<uint8_t, FLASH_PAGE_SIZE> src{};
    std::string sample = R"({"timestamp":"2026-09-18T08:00:00Z","level":"INFO","module":"kernel_pagefile","event_id":1042,"message":"Memory page evicted to compressed FTL swap cache successfully."})";
    for (size_t i = 0; i < FLASH_PAGE_SIZE; ++i) {
        src[i] = static_cast<uint8_t>(sample[i % sample.size()]);
    }

    CompressedBlock comp = LZ4Compressor::Compress(src.data());
    TEST_ASSERT(comp.is_compressed, "Text/JSON must be compressed");
    TEST_ASSERT(comp.compressed_size < 1024, "Repeated JSON should easily fit in 1KB slot");
    TEST_ASSERT(comp.slot == SlotAllocation::SLOT_1KB, "Must allocate SLOT_1KB");

    alignas(64) std::array<uint8_t, FLASH_PAGE_SIZE> dest{};
    dest.fill(0x00);
    bool ok = LZ4Compressor::Decompress(comp, dest.data());
    TEST_ASSERT(ok, "Decompression failed on JSON payload");
    TEST_ASSERT(std::memcmp(src.data(), dest.data(), FLASH_PAGE_SIZE) == 0, "Data mismatch on JSON payload");
    return true;
}

bool TestLZ4BinaryCodeLossless() {
    alignas(64) std::array<uint8_t, FLASH_PAGE_SIZE> src{};
    // Simulate x86-64 code: repetitive function prologues, calls, NOP paddings
    for (size_t i = 0; i < FLASH_PAGE_SIZE; i += 16) {
        src[i + 0] = 0x55; // push rbp
        src[i + 1] = 0x48; src[i + 2] = 0x89; src[i + 3] = 0xE5; // mov rbp, rsp
        src[i + 4] = 0x48; src[i + 5] = 0x83; src[i + 6] = 0xEC; src[i + 7] = 0x20; // sub rsp, 32
        src[i + 8] = 0x90; src[i + 9] = 0x90; src[i + 10] = 0x90; src[i + 11] = 0x90; // nop
        src[i + 12] = 0xC9; // leave
        src[i + 13] = 0xC3; // ret
        src[i + 14] = 0xCC; src[i + 15] = 0xCC; // int3 padding
    }

    CompressedBlock comp = LZ4Compressor::Compress(src.data());
    TEST_ASSERT(comp.is_compressed, "Binary code pattern must be compressed");
    TEST_ASSERT(comp.compressed_size < 2048, "Repetitive code must compress to <= 2KB slot");

    alignas(64) std::array<uint8_t, FLASH_PAGE_SIZE> dest{};
    bool ok = LZ4Compressor::Decompress(comp, dest.data());
    TEST_ASSERT(ok, "Decompression failed on binary code");
    TEST_ASSERT(std::memcmp(src.data(), dest.data(), FLASH_PAGE_SIZE) == 0, "Data mismatch on binary code");
    return true;
}

bool TestLZ4IncompressibleFallback() {
    alignas(64) std::array<uint8_t, FLASH_PAGE_SIZE> src{};
    std::mt19937_64 rng(1337);
    for (size_t i = 0; i < FLASH_PAGE_SIZE; i += sizeof(uint64_t)) {
        uint64_t val = rng();
        std::memcpy(src.data() + i, &val, sizeof(uint64_t));
    }

    CompressedBlock comp = LZ4Compressor::Compress(src.data());
    TEST_ASSERT(!comp.is_compressed, "High-entropy random noise must trigger fallback");
    TEST_ASSERT(comp.compressed_size == FLASH_PAGE_SIZE, "Size must be 4096 bytes");
    TEST_ASSERT(comp.slot == SlotAllocation::FULL_4KB, "Slot must be FULL_4KB");

    alignas(64) std::array<uint8_t, FLASH_PAGE_SIZE> dest{};
    bool ok = LZ4Compressor::Decompress(comp, dest.data());
    TEST_ASSERT(ok, "Decompression failed on incompressible fallback");
    TEST_ASSERT(std::memcmp(src.data(), dest.data(), FLASH_PAGE_SIZE) == 0, "Data mismatch on random noise");
    return true;
}

bool TestLZ4LatencyAndThroughput() {
    constexpr size_t NUM_BLOCKS = 1000;
    alignas(64) std::array<uint8_t, FLASH_PAGE_SIZE> block{};
    std::string text = "Telemetry log entry from Micron 2400 QLC SSD host write stream page allocation. ";
    for (size_t i = 0; i < FLASH_PAGE_SIZE; ++i) {
        block[i] = static_cast<uint8_t>(text[i % text.size()]);
    }

    auto start_comp = std::chrono::high_resolution_clock::now();
    CompressedBlock comp;
    for (size_t i = 0; i < NUM_BLOCKS; ++i) {
        comp = LZ4Compressor::Compress(block.data());
    }
    auto end_comp = std::chrono::high_resolution_clock::now();

    alignas(64) std::array<uint8_t, FLASH_PAGE_SIZE> dest{};
    auto start_decomp = std::chrono::high_resolution_clock::now();
    for (size_t i = 0; i < NUM_BLOCKS; ++i) {
        LZ4Compressor::Decompress(comp, dest.data());
    }
    auto end_decomp = std::chrono::high_resolution_clock::now();

    double comp_total_us = std::chrono::duration<double, std::micro>(end_comp - start_comp).count();
    double decomp_total_us = std::chrono::duration<double, std::micro>(end_decomp - start_decomp).count();

    double comp_per_block_us = comp_total_us / NUM_BLOCKS;
    double decomp_per_block_us = decomp_total_us / NUM_BLOCKS;
    double mb_processed = (NUM_BLOCKS * 4096.0) / (1024.0 * 1024.0);

    double comp_throughput_mbs = mb_processed / (comp_total_us / 1e6);
    double decomp_throughput_mbs = mb_processed / (decomp_total_us / 1e6);

    std::cout << "    [Perf] 4KB Block Compression Latency:   " << comp_per_block_us << " us/block (" << comp_throughput_mbs << " MB/s)\n";
    std::cout << "    [Perf] 4KB Block Decompression Latency: " << decomp_per_block_us << " us/block (" << decomp_throughput_mbs << " MB/s)\n";

    TEST_ASSERT(comp_per_block_us < 5.0, "Compression latency should be < 5.0 us");
    TEST_ASSERT(decomp_per_block_us < 2.0, "Decompression latency should be < 2.0 us");
    return true;
}

#include "flash_model.hpp"
#include "ftl_controller.hpp"

bool TestFlashModelNANDPhysicalRules() {
    FlashConfig cfg;
    cfg.page_size_bytes = 4096;
    cfg.pages_per_block = 64;
    cfg.total_blocks = 16;
    FlashModel flash(cfg);

    alignas(64) std::array<uint8_t, 4096> page1{};
    page1.fill(0x33);

    // First program must succeed
    bool ok = flash.ProgramPage(0, 0, page1.data());
    TEST_ASSERT(ok, "Initial page program must succeed");

    // Programming the SAME page without erasing MUST fail (NAND physical rule!)
    ok = flash.ProgramPage(0, 0, page1.data());
    TEST_ASSERT(!ok, "Overwriting programmed NAND page without block erase must fail!");

    // Read back page
    alignas(64) std::array<uint8_t, 4096> read_buf{};
    ok = flash.ReadPage(0, 0, read_buf.data());
    TEST_ASSERT(ok, "Read page must succeed");
    TEST_ASSERT(std::memcmp(page1.data(), read_buf.data(), 4096) == 0, "Page data mismatch");

    // Erase block
    ok = flash.EraseBlock(0);
    TEST_ASSERT(ok, "Block erase must succeed");
    TEST_ASSERT(flash.GetEraseCount(0) == 1, "Erase count must be 1");

    // Programming after erase must now succeed
    ok = flash.ProgramPage(0, 0, page1.data());
    TEST_ASSERT(ok, "Program after block erase must succeed");
    return true;
}

bool TestFTLInLineHalfPagePackingAndWAF() {
    FlashConfig flash_cfg;
    flash_cfg.page_size_bytes = 4096;
    flash_cfg.pages_per_block = 64;
    flash_cfg.total_blocks = 32;

    // 4 MB logical capacity = 1024 LBAs
    FTLController ftl(4 * 1024 * 1024, flash_cfg);

    // Write 100 compressible blocks (simulating browser cache and JSON logs)
    std::string json_sample = R"({"trace_id":"req-99120","service":"browser_cache","payload":"cached DOM element tree and stylesheet rules for rendering engine"})";
    std::vector<std::vector<uint8_t>> written_blocks(100, std::vector<uint8_t>(4096));

    for (size_t i = 0; i < 100; ++i) {
        for (size_t b = 0; b < 4096; ++b) {
            written_blocks[i][b] = static_cast<uint8_t>(json_sample[(b + i) % json_sample.size()]);
        }
        bool ok = ftl.WriteBlock(i, written_blocks[i].data());
        TEST_ASSERT(ok, "Failed to write compressible block to FTL");
    }

    ftl.FlushStagedWrites();

    // Verify 100% lossless readback for all 100 blocks
    alignas(64) std::array<uint8_t, 4096> read_buf{};
    for (size_t i = 0; i < 100; ++i) {
        bool ok = ftl.ReadBlock(i, read_buf.data());
        TEST_ASSERT(ok, "Failed to read block from FTL");
        TEST_ASSERT(std::memcmp(written_blocks[i].data(), read_buf.data(), 4096) == 0, "Readback mismatch on compressible block");
    }

    FTLTelemetry tel = ftl.GetTelemetry();
    std::cout << "    [WAF Telemetry] Host Writes: " << tel.total_host_writes 
              << " (" << (tel.total_host_bytes_written / 1024) << " KB)\n";
    std::cout << "    [WAF Telemetry] Flash Pages Programmed: " << tel.flash_tel.total_pages_programmed 
              << " (" << (tel.flash_tel.total_physical_bytes_written / 1024) << " KB)\n";
    std::cout << "    [WAF Telemetry] Half-Page Packs: " << tel.compressed_half_pages_packed << "\n";
    std::cout << "    [WAF Telemetry] Measured WAF: " << tel.write_amplification_factor << "\n";

    // Because 100 compressible blocks were packed 2-per-page, only 50 physical pages were written!
    TEST_ASSERT(tel.total_host_writes == 100, "Must be 100 host writes");
    TEST_ASSERT(tel.flash_tel.total_pages_programmed <= 51, "Must have programmed <= 51 physical pages");
    TEST_ASSERT(tel.write_amplification_factor <= 0.52, "WAF must be <= 0.52 on compressible data!");
    return true;
}

bool TestFTLSparseZeroDeduplication() {
    FlashConfig flash_cfg;
    FTLController ftl(2 * 1024 * 1024, flash_cfg);

    alignas(64) std::array<uint8_t, 4096> zero_buf{};
    zero_buf.fill(0x00);

    // Write 50 all-zero blocks (typical OS pagefile initialization)
    for (size_t i = 0; i < 50; ++i) {
        bool ok = ftl.WriteBlock(i, zero_buf.data());
        TEST_ASSERT(ok, "Zero block write must succeed");
    }

    // Verify readback
    alignas(64) std::array<uint8_t, 4096> read_buf{};
    for (size_t i = 0; i < 50; ++i) {
        read_buf.fill(0xFF);
        bool ok = ftl.ReadBlock(i, read_buf.data());
        TEST_ASSERT(ok, "Zero block read must succeed");
        TEST_ASSERT(std::memcmp(zero_buf.data(), read_buf.data(), 4096) == 0, "Zero block read mismatch");
    }

    FTLTelemetry tel = ftl.GetTelemetry();
    TEST_ASSERT(tel.zero_blocks_filtered == 50, "All 50 zero blocks must be filtered");
    TEST_ASSERT(tel.flash_tel.total_pages_programmed == 0, "Zero pages must be written to flash");
    TEST_ASSERT(tel.write_amplification_factor == 0.0, "WAF must be 0.0 for all-zero stream");
    return true;
}

int main() {
    std::cout << "========================================================\n";
    std::cout << "  zFTL Verification & Unit Test Suite (Storage Core)    \n";
    std::cout << "========================================================\n";

    // Legacy 64-byte BDI tests
    RUN_TEST(TestZerosLossless);
    RUN_TEST(TestRepeatedWordLossless);
    RUN_TEST(TestBase8Delta1Lossless);
    RUN_TEST(TestBase4Delta1Lossless);
    RUN_TEST(TestIncompressibleFallback);
    RUN_TEST(TestLineTableAllocationAndCompaction);
    RUN_TEST(TestHyperRAMControllerByteAddressable);
    RUN_TEST(TestVirtualCapacityDoublerStress);

    // Phase 2: 4KB Storage Block LZ4 tests
    std::cout << "\n--- Phase 2: 4 KB Storage Block LZ4 Verification ---\n";
    RUN_TEST(TestLZ4ZeroBlockLossless);
    RUN_TEST(TestLZ4TextJsonLossless);
    RUN_TEST(TestLZ4BinaryCodeLossless);
    RUN_TEST(TestLZ4IncompressibleFallback);
    RUN_TEST(TestLZ4LatencyAndThroughput);

    // Phase 3: Flash Translation Layer & Physical NAND Flash Tests
    std::cout << "\n--- Phase 3: FTL In-Line Compression & Flash WAF Tests ---\n";
    RUN_TEST(TestFlashModelNANDPhysicalRules);
    RUN_TEST(TestFTLInLineHalfPagePackingAndWAF);
    RUN_TEST(TestFTLSparseZeroDeduplication);

    std::cout << "========================================================\n";
    std::cout << "  Summary: " << g_tests_passed << " Passed, " << g_tests_failed << " Failed\n";
    std::cout << "========================================================\n";

    return (g_tests_failed == 0) ? 0 : 1;
}
