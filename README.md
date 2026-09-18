# zFTL: In-Line Compressed Flash Translation Layer for QLC SSDs

[![Standard C++20](https://img.shields.io/badge/Language-C%2B%2B20-blue.svg)](https://en.cppreference.com/w/cpp/20)
[![Target-Flash](https://img.shields.io/badge/Target-3D%20QLC%20NAND%20Flash-orange.svg)](https://www.micron.com)
[![License: MIT](https://img.shields.io/badge/License-MIT-green.svg)](file:///d:/hyper_ram/LICENSE)

> **An architectural simulator and digital hardware design for an in-line hardware-compressed Flash Translation Layer (FTL).**  
> *Designed to reduce Write Amplification Factor (WAF) and extend the physical endurance of budget QLC SSDs (like the Micron 2400 512GB).*

---

## 💡 The Real-World Problem: The QLC SSD Endurance Crisis

Consumer solid-state drives in budget laptops and student computers (e.g., Lenovo LOQ, HP Victus, Dell Inspiron) increasingly ship with high-density **3D QLC (Quad-Level Cell) NAND Flash** (such as the Micron 2400 512GB NVMe SSD).

While QLC enables affordable storage, it comes with severe physical tradeoffs:
1. **Extremely Low Write Endurance**:
   * Storing 4 bits per cell requires managing 16 precise voltage states.
   * A typical 512GB QLC SSD is officially rated for only **~150 TBW (Terabytes Written)**, compared to 300–600 TBW for more expensive TLC drives.
2. **Heavy Background OS Wear**:
   * Everyday operating system activity—virtual memory paging, Chrome/Edge browser caches, and Windows telemetry logs—constantly writes tens of gigabytes to disk every day.
   * These non-stop background writes rapidly degrade flash health (often consuming 8% to 10% of total drive lifespan within the first year).
3. **Slow Flash Program Latencies**:
   * Programming physical QLC flash cells is slow (500 µs to 1,500 µs per page).

---

## ⚡ The Solution: In-Line FTL Hardware Compression

Instead of requiring expensive enterprise flash dies, **zFTL** attacks the problem inside the **SSD Controller's Flash Translation Layer (FTL)**:

```
[ Host OS (Windows / Linux) ]
              │  Writes 4 KB Logical Blocks (LBAs)
              ▼
    [ zFTL Controller ]
              │
              ├──► [ Real-Time Compression Engine (LZ4) ]  (~1 µs)
              │         Shrinks compressible 4 KB data to ~1.5 - 2 KB
              │
              ├──► [ Chunk Packing & LBA Mapping Table ]
              │         Packs two compressed blocks into a single 4 KB physical flash page
              ▼
   [ Physical 3D QLC NAND Flash ]  (Only 1 physical page programmed instead of 2!)
```

### Key Architectural Benefits:
* **Cuts Physical Writes in Half**: On compressible system traffic (logs, browser cache, pagefile heaps), writing half as much data drops the **Write Amplification Factor (WAF)** below 1.0 (from 1.3+ down to ~0.65).
* **Doubles Drive Lifespan**: A 150 TBW budget QLC drive with a 0.65 WAF effectively endures the equivalent of **230+ TBW of host writes**.
* **Faster Write Speeds**: Spending 1 microsecond on hardware compression eliminates 500+ microseconds of slow physical flash programming.
* **Virtual Capacity Expansion**: Allows a 512GB drive to hold significantly more user data before filling up.

---

## 📁 Repository Structure & AI Context

* [`AGENTS.md`](file:///d:/hyper_ram/AGENTS.md) — **Persistent instructions and architectural boundaries for AI assistants.** Read this before suggesting code or starting new sessions.
* [`PROGRESS.md`](file:///d:/hyper_ram/PROGRESS.md) — **Living roadmap and active task tracker.** Check this to resume work seamlessly.
* [`include/`](file:///d:/hyper_ram/include/) — C++ headers for the compression engine, flash array model, and FTL mapping table.
* [`src/`](file:///d:/hyper_ram/src/) — Core C++ simulator implementation and benchmark telemetry runner.
* [`hdl/`](file:///d:/hyper_ram/hdl/) — Synthesizable Verilog RTL designs for hardware acceleration.
* [`build.ps1`](file:///d:/hyper_ram/build.ps1) — One-click build and verification script for Windows PowerShell.

---

## 🛠️ How to Build and Run Locally

### Requirements
* **Compiler**: Visual Studio 2022 (MSVC with C++ Desktop Development) or Clang/GCC with C++20 support.
* **Build Tool**: CMake 3.20+ (included with Visual Studio).

### Quick Build & Test (PowerShell)
```powershell
# Run one-click build and test suite
.\build.ps1
```

### Manual Build with CMake
```powershell
# 1. Configure build directory
cmake -B build -G "Visual Studio 17 2022" -A x64

# 2. Build in Release Mode
cmake --build build --config Release

# 3. Run the Verification Suite
.\build\Release\hyper_ram_tests.exe
```

---

## 🗺️ Project Roadmap

See [`PROGRESS.md`](file:///d:/hyper_ram/PROGRESS.md) for the active milestone breakdown, current tasks, and recent changes.
