// Copyright (c) 2026 zFTL Project. All Rights Reserved.
// In-Line Compressed Flash Translation Layer Architecture Benchmark.
#include "ftl_controller.hpp"
#include <iostream>
#include <vector>
#include <chrono>
#include <iomanip>
#include <random>
#include <string>
#include <functional>

namespace {

void PrintBanner() {
    std::cout << "\033[1;36m";
    std::cout << R"(
    ===================================================================
       _____ _____ _     
      |__  /|  ___| |_| |      zFTL: Compressed Flash Translation Layer
        / / | |_  | __| |      In-Line Storage Compression for QLC SSDs
       / /_ |  _| | |_| |___   Endurance Multiplier for Budget Silicon
      /____||_|    \__|_____|  Reference Target: Micron 2400 QLC (150 TBW)
    ===================================================================
    )" << "\033[0m\n";
}

void RunWorkload(const std::string& name,
                 zftl::FTLController& ftl,
                 size_t num_blocks,
                 const std::function<void(size_t lba, uint8_t* buf)>& generator) {
    std::cout << "\n\033[1;33m[*] Running Workload: " << name << " (" 
              << (num_blocks * 4 / 1024) << " MB)...\033[0m\n";

    ftl.ResetTelemetry();
    alignas(64) std::array<uint8_t, zftl::FLASH_PAGE_SIZE> block{};

    auto start_time = std::chrono::high_resolution_clock::now();

    for (size_t i = 0; i < num_blocks; ++i) {
        generator(i, block.data());
        ftl.WriteBlock(i, block.data());
    }
    ftl.FlushStagedWrites();

    auto end_time = std::chrono::high_resolution_clock::now();
    double elapsed_ms = std::chrono::duration<double, std::milli>(end_time - start_time).count();

    zftl::FTLTelemetry tel = ftl.GetTelemetry();

    double host_mb = tel.total_host_bytes_written / (1024.0 * 1024.0);
    double flash_mb = tel.flash_tel.total_physical_bytes_written / (1024.0 * 1024.0);
    double throughput_mb_s = host_mb / (elapsed_ms / 1000.0);

    // Life extension factor: 1.0 / WAF
    double life_multiplier = (tel.write_amplification_factor > 0.0) 
                             ? (1.0 / tel.write_amplification_factor) 
                             : 99.9;
    double effective_tbw = 150.0 * life_multiplier; // Based on Micron 2400 150 TBW rating

    std::cout << "-------------------------------------------------------------------\n";
    std::cout << std::left << std::setw(36) << "Logical Host Writes:" 
              << tel.total_host_writes << " blocks (" << std::fixed << std::setprecision(2) << host_mb << " MB)\n";
    std::cout << std::left << std::setw(36) << "Physical Flash Writes:" 
              << tel.flash_tel.total_pages_programmed << " pages (" << flash_mb << " MB)\n";
    std::cout << std::left << std::setw(36) << "Sparse Zero Pages Skipped:" 
              << tel.zero_blocks_filtered << "\n";
    std::cout << std::left << std::setw(36) << "Half-Page Compressed Pairs:" 
              << tel.compressed_half_pages_packed << "\n";
    std::cout << std::left << std::setw(36) << "Raw Incompressible Pages:" 
              << tel.raw_full_pages_written << "\n";
    std::cout << std::left << std::setw(36) << "Processing Throughput:" 
              << "\033[1;32m" << throughput_mb_s << " MB/s\033[0m\n";
    std::cout << "-------------------------------------------------------------------\n";
    std::cout << std::left << std::setw(36) << "Standard QLC Baseline WAF:" << "1.25 - 1.50\n";
    std::cout << std::left << std::setw(36) << "zFTL Measured WAF:" 
              << "\033[1;32m" << tel.write_amplification_factor << "\033[0m\n";
    std::cout << std::left << std::setw(36) << "Physical Flash Wear Reduction:" 
              << "\033[1;32m" << std::max(0.0, (1.0 - tel.write_amplification_factor) * 100.0) << " %\033[0m\n";
    std::cout << std::left << std::setw(36) << "Effective Micron 2400 Lifespan:" 
              << "\033[1;36m" << effective_tbw << " TBW (Baseline: 150 TBW)\033[0m\n";
    std::cout << "-------------------------------------------------------------------\n";
}

} // namespace

int main() {
    PrintBanner();

    zftl::FlashConfig flash_cfg;
    flash_cfg.page_size_bytes = 4096;
    flash_cfg.pages_per_block = 64;
    flash_cfg.total_blocks = 256; // 64 MB physical flash pool

    // 64 MB logical capacity = 16,384 LBAs
    zftl::FTLController ftl(64 * 1024 * 1024, flash_cfg);

    // 1. Windows Pagefile & Virtual Memory Heap (mix of zeroes and pointers)
    RunWorkload("Windows Pagefile / Heap Allocation", ftl, 2048, [](size_t lba, uint8_t* buf) {
        if (lba % 3 == 0) {
            std::memset(buf, 0, 4096); // Sparse zero pages
        } else {
            for (size_t b = 0; b < 4096; b += sizeof(uint64_t)) {
                uint64_t ptr = 0x00007FFF00100000ULL + (b * 8);
                std::memcpy(buf + b, &ptr, sizeof(uint64_t));
            }
        }
    });

    // 2. Browser Cache & Telemetry Logs (JSON, strings, metadata)
    std::string sample_json = R"({"time":"2026-09-18T08:00:00Z","type":"cache_entry","url":"https://cdn.example.com/assets/app.chunk.js","headers":{"content-type":"application/javascript","cache-control":"max-age=31536000"}})";
    RunWorkload("Chrome/Edge Browser Cache & Web Assets", ftl, 2048, [&](size_t lba, uint8_t* buf) {
        for (size_t b = 0; b < 4096; ++b) {
            buf[b] = static_cast<uint8_t>(sample_json[(b + lba) % sample_json.size()]);
        }
    });

    // 3. High-Entropy Encrypted / Compressed Media (Worst-Case Fallback)
    std::mt19937_64 rng(42);
    RunWorkload("Encrypted Media / Random Noise (Worst-Case)", ftl, 2048, [&](size_t /*lba*/, uint8_t* buf) {
        for (size_t b = 0; b < 4096; b += sizeof(uint64_t)) {
            uint64_t val = rng();
            std::memcpy(buf + b, &val, sizeof(uint64_t));
        }
    });

    // 4. Realistic Blended Daily Workload
    // (35% Zero Pages, 40% JSON/Logs/Cache, 15% Binary Code, 10% Encrypted)
    RunWorkload("Blended Real-World Daily Laptop Profile", ftl, 4096, [&](size_t lba, uint8_t* buf) {
        size_t mod = lba % 20;
        if (mod < 7) {
            // 35% Sparse zeros
            std::memset(buf, 0, 4096);
        } else if (mod < 15) {
            // 40% Browser / Web assets
            for (size_t b = 0; b < 4096; ++b) {
                buf[b] = static_cast<uint8_t>(sample_json[(b + lba) % sample_json.size()]);
            }
        } else if (mod < 18) {
            // 15% Structured code
            for (size_t b = 0; b < 4096; b += 4) {
                buf[b] = 0x48; buf[b+1] = 0x89; buf[b+2] = 0x5C; buf[b+3] = 0x24;
            }
        } else {
            // 10% Random noise
            for (size_t b = 0; b < 4096; b += sizeof(uint64_t)) {
                uint64_t val = rng();
                std::memcpy(buf + b, &val, sizeof(uint64_t));
            }
        }
    });

    std::cout << "\n\033[1;32m[+] All Architectural Workloads Evaluated Successfully!\033[0m\n\n";
    return 0;
}
