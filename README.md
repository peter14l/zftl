# zFTL: In-Line Compressed Flash Translation Layer for QLC NAND Flash

[![Language: C++20](https://img.shields.io/badge/Language-C%2B%2B20-blue.svg)](https://en.cppreference.com/w/cpp/20)
[![HDL: Verilog](https://img.shields.io/badge/HDL-Verilog%20RTL-purple.svg)](https://en.wikipedia.org/wiki/Verilog)
[![Target: 3D QLC NAND](https://img.shields.io/badge/Target-3D%20QLC%20NAND%20Flash-orange.svg)](https://www.micron.com)
[![Tests: 20/20](https://img.shields.io/badge/Tests-20%2F20%20Passing-brightgreen.svg)]()
[![License: MIT](https://img.shields.io/badge/License-MIT-green.svg)](LICENSE)

> **An architectural simulator and synthesizable hardware IP for an in-line compression-enabled Flash Translation Layer (FTL), designed to reduce Write Amplification Factor (WAF) and extend physical endurance on consumer QLC NAND SSDs.**
>
> **Measured WAF: 0.40 on blended real-world workloads → Effective lifespan 375 TBW (vs. 150 TBW baseline on Micron 2400 QLC).**

---

## 🔬 The Problem: QLC NAND Flash Has a Write Endurance Crisis

Consumer SSDs in budget laptops (Lenovo LOQ, HP Victus, Dell Inspiron) increasingly ship with **3D QLC (Quad-Level Cell) NAND flash** — such as the **Micron 2400 512 GB NVMe**. QLC stores 4 bits per cell across 16 voltage states, enabling high-density, low-cost storage. However, the physics imposes a severe endurance penalty:

| Flash Type | Bits/Cell | P/E Cycles | TBW (512 GB) |
|:---|:---:|:---:|:---:|
| SLC | 1 | ~100,000 | ~tens of PB |
| TLC | 3 | ~1,000 | ~300–600 TBW |
| **QLC** | **4** | **~500–1,500** | **~100–150 TBW** |

Everyday OS workloads — virtual memory paging, browser caches, Windows/Linux telemetry — write tens of gigabytes to the SSD every day, consuming flash endurance continuously. A 150 TBW drive under typical student laptop use can exhaust its rated endurance in under 3 years.

---

## ⚡ The Solution: In-Line FTL Compression

**zFTL** intercepts every 4 KB host write at the FTL layer, compresses it transparently using LZ4, and packs two compressed blocks into one physical flash page — halving the number of physical flash program operations:

```
  Host OS
  ───────
  4 KB Write (LBA n)         4 KB Write (LBA n+1)
       │                           │
       └──────────┬────────────────┘
                  ▼
        ┌─────────────────────┐
        │  LZ4 Compressor     │  ~0.5 µs/block  (~8.3 GB/s throughput)
        │  4 KB → ~1.5–2 KB   │
        └────────┬────────────┘
                 │
                 ▼
        ┌─────────────────────┐
        │  Half-Page Packer   │  Two 2 KB payloads → one 4 KB flash page
        │  LBA → PBA Mapping  │
        └────────┬────────────┘
                 │
                 ▼
  ┌──────────────────────────────┐
  │  Physical 3D QLC NAND Flash  │  1 page programmed (was 2)  → WAF = 0.5
  │  Page program: ~800 µs       │  Zero blocks: skipped entirely → WAF = 0.0
  └──────────────────────────────┘
```

### Optimization Layers

| Optimization | Mechanism | WAF Impact |
|:---|:---|:---:|
| **Sparse-Zero Filtering** | All-zero 4 KB blocks never touch flash | 0.0 |
| **Half-Page Packing** | Two ≤2 KB compressed blocks share one physical page | ~0.5 |
| **LZ4 Compression** | Sub-2KB compression on typical OS traffic (logs, cache, heap) | 0.4–0.7 |
| **Incompressible Fallback** | High-entropy data (encrypted, media) stored raw, no corruption | 1.0 |
| **Greedy GC** | Victim block selected by max invalid-page count | Minimizes write overhead |

---

## 📊 Benchmark Results

All results measured on a 64 MB simulated QLC flash pool (256 blocks × 64 pages × 4 KB).  
Baseline reference: **Micron 2400 512 GB QLC NVMe — 150 TBW rated endurance.**

| Workload | WAF | Wear Reduction | Effective Lifespan |
|:---|:---:|:---:|:---:|
| Windows Pagefile / Heap | 0.67 | 33% | **225 TBW** |
| Chrome/Edge Browser Cache | 0.50 | 50% | **300 TBW** |
| Blended Real-World Laptop | **0.40** | **60%** | **375 TBW** |
| Encrypted / Random Noise | 1.00 | 0% | 150 TBW (graceful fallback) |

> LZ4 compression throughput: **~8.3 GB/s per core** (0.47 µs / 4 KB block, measured on Ryzen 5 7535HS).

---

## 🏗️ Architecture

### Software Simulation Stack (C++20)

```
include/
├── lz4_compressor.hpp      # 4 KB block LZ4 engine — Compress(), Decompress(), QuantizeSlot()
├── flash_model.hpp         # Physical 3D QLC NAND array — ProgramPage(), EraseBlock(), P/E tracking
└── ftl_controller.hpp      # FTL: LBA→PBA mapping, half-page packing, greedy GC, telemetry

src/
├── lz4.c / lz4.h           # Open-source LZ4 reference implementation
├── lz4_compressor.cpp      # Compression engine implementation
├── flash_model.cpp         # NAND physical model (latencies, wear counters, NAND constraints)
├── ftl_controller.cpp      # Full FTL: write path, read path, GC with valid-page migration
├── main.cpp                # Workload benchmark runner & WAF telemetry reporter
└── tests.cpp               # 20-test verification suite (LZ4 + FlashModel + FTLController)
```

### Hardware RTL (Synthesizable Verilog)

```
hdl/
├── lz4_decompressor_4k.v   # Pipelined LZ4 byte-stream decompressor (9-state FSM)
│                             AXI4-Stream I/O, 4 KB history RAM → synthesizes to BRAM
│                             Target clock: 250 MHz (functional simulation verified)
└── tb_lz4_decompressor.v   # Self-checking testbench
```

### Garbage Collection Algorithm

```
RunGarbageCollectionIfNeeded():
  Phase 1 — Greedy victim selection:
    For each block b (skip reserve_block):
      if invalid_page_count[b] > max → victim = b

  Phase 2 — Valid-page migration:
    For each valid page p in victim:
      tmp ← ReadPage(victim, p)
      ProgramPage(reserve_block, write_ptr, tmp)
      Remap lba_table[lba].{block, page} → (reserve_block, write_ptr)

  Phase 3 — Erase & reuse:
    EraseBlock(victim)
    active_block ← victim
    Rotate reserve_block pointer
```

---

## 🧪 Test Suite (20/20 Passing)

```
--- Section 1: LZ4Compressor (6 tests) ---
  Zero-block detection, round-trip losslessness (JSON, binary, noise), slot quantization

--- Section 2: FlashModel (5 tests) ---
  ProgramPage/ReadPage, NAND overwrite constraint, erase/reset, invalidation, telemetry

--- Section 3: FTLController (8 tests) ---
  Sparse-zero bypass, half-page packing, incompressible fallback, overwrite invalidation,
  unmapped LBA reads, WAF on compressible/incompressible workloads, GC data integrity

--- Section 4: End-to-End (1 test) ---
  Blended workload WAF regression (must be < 0.8)

ALL 20/20 TESTS PASSED ✓
```

---

## 🛠️ Build & Run

**Requirements**: Visual Studio 2022 (MSVC, C++ Desktop workload) or GCC/Clang with C++20 support + CMake 3.20+.

```powershell
# One-click: build + run all tests
.\build.ps1

# Run WAF benchmark
.\build\Release\zftl_sim.exe

# Run test suite only
.\build\Release\zftl_tests.exe
```

**Manual CMake:**
```powershell
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
.\build\Release\zftl_tests.exe
```

---

## 📂 Repository Layout

```
zFTL/
├── AGENTS.md               # AI assistant persistent context (read before editing)
├── PROGRESS.md             # Living roadmap & milestone tracker
├── CMakeLists.txt          # C++20 build (MSVC + GCC/Clang)
├── build.ps1               # One-click Windows build & test script
├── include/                # C++ headers (LZ4 engine, NAND model, FTL controller)
├── src/                    # Implementation + benchmarks + 20-test suite
├── hdl/                    # Synthesizable Verilog RTL (LZ4 decompressor + testbench)
├── docs/                   # Architecture spec, paper draft, diagrams
└── legacy/                 # Historical BDI/HyperRAM code (not compiled)
```

---

## 📖 Related Work & References

- Lim, S. et al. *"Transparent Compression for SSD-based Storage."* SYSTOR 2013.
- Collet, Y. *LZ4 — Extremely Fast Compression Algorithm.* [lz4.org](https://lz4.org)
- Agrawal, N. et al. *"Design Tradeoffs for SSD Performance."* USENIX ATC 2008.
- He, J. et al. *"Reducing Write Amplification of Flash Storage through Cooperative Data Management with NVM."* ACM SYSTOR 2017.
- Micron Technology. *"Micron 2400 NVMe SSD Product Brief."* 2022.

---

## 👤 Author

**Undergraduate B.Tech Student — Electronics & Communication Engineering (ECE)**  
Motivated by first-hand experience with QLC SSD endurance degradation on budget student hardware.  
Project scope: Architectural simulation → Synthesizable RTL → Academic publication pathway.

---

*This project is an open-hardware, academic research initiative. All benchmark results are cycle-accurate simulation metrics, not manufacturer datasheets.*
