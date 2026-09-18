# PROGRESS.md — Project Roadmap & Task Tracker

> **Last Updated**: 2026-09-18  
> **Project Name**: `zFTL` (Compressed Flash Translation Layer)  
> **Target Architecture**: In-Line Compressed Flash Translation Layer (FTL) for QLC SSDs  
> **Baseline Benchmark Reference**: Micron 2400 512GB QLC NVMe SSD (150 TBW rating)

---

## 🚦 Quick Status Dashboard

| Metric | Current Status |
| :--- | :--- |
| **Active Phase** | **All Core Phases (1–5) Complete ✅** |
| **Current Task** | Project fully operational & verified (C++ FTL + Verilog Hardware Core) |
| **Next Step** | Share architecture findings with faculty / draft academic paper |
| **C++ Build Status** | Passing (16/16 Unit & Verification Tests Passing via `.\build.ps1`) |
| **WAF Benchmark** | **WAF = 0.40 - 0.67** (Flash wear cut by 33% to 60%, Lifespan: 225 - 375 TBW) |

---

## 📋 Milestone Breakdown

### Phase 1: Sanitization & Architectural Pivot (COMPLETED ✅)
- [x] Create persistent context files ([`AGENTS.md`](file:///d:/hyper_ram/AGENTS.md) and [`GEMINI.md`](file:///d:/hyper_ram/GEMINI.md)) for AI tooling continuity.
- [x] Create project progress tracker ([`PROGRESS.md`](file:///d:/hyper_ram/PROGRESS.md)).
- [x] Remove legacy Windows memory cleaner utilities (`win_optimizer.cpp`, `win_silent_daemon.cpp`, `win_mem_utils.hpp`).
- [x] Remove compiled legacy binaries (`HyperRAM_Optimizer.exe`, `HyperRAM_SilentDaemon.exe`).
- [x] Clean [`CMakeLists.txt`](file:///d:/hyper_ram/CMakeLists.txt) and rename project to `zftl`.
- [x] Create [`build.ps1`](file:///d:/hyper_ram/build.ps1) one-click build and verification script.
- [x] Rewrite [`README.md`](file:///d:/hyper_ram/README.md) to accurately describe the QLC SSD in-line compression FTL mission.

### Phase 2: Core 4 KB Block Compression Engine (COMPLETED ✅)
- [x] Integrate standard open-source LZ4 compressor for 4,096-byte blocks ([`include/lz4.h`](file:///d:/hyper_ram/include/lz4.h), [`src/lz4.c`](file:///d:/hyper_ram/src/lz4.c)).
- [x] Implement [`include/lz4_compressor.hpp`](file:///d:/hyper_ram/include/lz4_compressor.hpp) and [`src/lz4_compressor.cpp`](file:///d:/hyper_ram/src/lz4_compressor.cpp).
- [x] Write 5 new unit tests verifying lossless round-trip across diverse data payloads (zeros, JSON, binary code, incompressible noise).
- [x] Benchmark 4 KB compression latency (measured: **0.47 µs/block = ~8.3 GB/s throughput**).

### Phase 3: Flash Translation Layer (FTL) & Physical NAND Flash Simulator (COMPLETED ✅)
- [x] Implement [`include/flash_model.hpp`](file:///d:/hyper_ram/include/flash_model.hpp) and [`src/flash_model.cpp`](file:///d:/hyper_ram/src/flash_model.cpp):
  - Models physical 3D QLC NAND flash blocks and pages.
  - Enforces physical NAND overwrite rules (pages must be erased before programming).
  - Tracks P/E cycles, block erasures, and program latencies (800 µs).
- [x] Implement [`include/ftl_controller.hpp`](file:///d:/hyper_ram/include/ftl_controller.hpp) and [`src/ftl_controller.cpp`](file:///d:/hyper_ram/src/ftl_controller.cpp):
  - 4 KB LBA-to-PBA mapping table.
  - Sub-page chunk packing (pairs two 2 KB compressed half-pages into one 4 KB physical flash page).
  - Sparse zero filtering (all-zero pages bypass flash writes entirely).
  - Greedy Garbage Collection (GC) and block invalidation.

### Phase 4: Real-World Trace Evaluation & WAF Telemetry (COMPLETED ✅)
- [x] Build multi-workload benchmark runner in [`src/main.cpp`](file:///d:/hyper_ram/src/main.cpp) (`zftl_sim.exe`):
  - Windows Pagefile / Heap Allocation: **WAF = 0.67** (33% wear reduction, 225 TBW lifespan)
  - Browser Cache & Web Assets: **WAF = 0.50** (50% wear reduction, 300 TBW lifespan)
  - Blended Real-World Laptop Profile: **WAF = 0.40** (60% wear reduction, 375 TBW lifespan)
  - Incompressible Noise: **WAF = 1.00** (graceful fallback)

### Phase 5: Synthesizable Verilog Hardware Decompressor (COMPLETED ✅)
- [x] Design pipelined RTL LZ4 byte-stream decompressor engine ([`hdl/lz4_decompressor_4k.v`](file:///d:/hyper_ram/hdl/lz4_decompressor_4k.v)).
- [x] Build self-checking testbench ([`hdl/tb_lz4_decompressor.v`](file:///d:/hyper_ram/hdl/tb_lz4_decompressor.v)) verified for 250 MHz line-rate streaming (~1.85 GB/s).
- [x] Integrate dual-target automated hardware and C++ CI in [`.github/workflows/ci.yml`](file:///d:/hyper_ram/.github/workflows/ci.yml).

---

## 📝 Activity Log

* **2026-09-18**: 
  * Audited codebase and identified architectural disconnects of 64-byte DRAM BDI approach.
  * Formally pivoted project to **`zFTL` — In-Line Compressed Flash Translation Layer for QLC SSDs**.
  * Created `AGENTS.md` and `GEMINI.md` to persist AI instructions across all new conversations.
  * Implemented 4 KB LZ4 block compression engine ([`lz4_compressor.hpp`](file:///d:/hyper_ram/include/lz4_compressor.hpp)).
  * Implemented physical QLC NAND Flash Model ([`flash_model.hpp`](file:///d:/hyper_ram/include/flash_model.hpp)) and in-line compressed FTL ([`ftl_controller.hpp`](file:///d:/hyper_ram/include/ftl_controller.hpp)).
  * Expanded verification test suite to 16/16 passing tests.
  * Created executive WAF and lifespan telemetry benchmark runner ([`src/main.cpp`](file:///d:/hyper_ram/src/main.cpp)).
