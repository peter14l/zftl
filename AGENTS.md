# AGENTS.md — AI Agent Guidance & Project Context

> **To all AI assistants (Antigravity, Cursor, Claude Code, Copilot, etc.):**  
> Read this file and `PROGRESS.md` at the start of EVERY conversation before reading other files or suggesting code.

---

## 1. Project Mission & Identity

* **Project Name**: `zFTL` / Compressed QLC Flash Storage Simulator
* **Author**: Undergraduate B.Tech Student in Electronics & Communication Engineering (ECE)
* **Real-World Motivation**: Modern consumer SSDs in budget laptops (such as the Micron 2400 512GB) use high-density **QLC NAND flash**, which has very low physical write endurance (~150 TBW) and slow write speeds. Everyday operating system writes (paging, browser caches, updates) wear out these drives rapidly.
* **Core Technical Goal**: Build an architectural simulator and hardware IP for an **in-line hardware-compressed Flash Translation Layer (FTL)**. By transparently compressing 4 KB host blocks (via LZ4 / byte-stream compression) before writing to physical NAND flash pages:
  1. **Write Amplification Factor (WAF)** is reduced below 1.0 (typical: 0.6–0.7 on compressible data).
  2. **Physical flash wear is halved**, effectively doubling the drive's operational lifespan.
  3. **Effective storage capacity is expanded** without adding physical flash dies.

---

## 2. Architectural Boundaries & Strict Rules

When generating code, documentation, or suggestions, **adhere strictly to these principles**:

1. **Target Storage, Not DRAM**:
   * The basic granularity is **4 KB (4,096-byte) disk sectors/pages**, NOT 64-byte CPU cache lines.
   * Compression algorithm is **LZ4 / dictionary stream compression**, NOT 64-byte BDI.
2. **No Hallucinated Silicon Specs**:
   * Never generate fake TSMC 28nm/16nm/7nm PPA tables, gate counts, or commercial datasheets unless they are produced by an actual open-source synthesis tool (like Yosys/OpenLane).
   * Frame all results as **architectural cycle-accurate simulation metrics**.
3. **No Snake-Oil Software Utilities**:
   * Do NOT re-introduce Windows `EmptyWorkingSet()` or OS memory cleaner scripts. This project is strictly about storage architecture, FTL firmware, and digital hardware.
4. **No Fictional Materials (e.g., FTJ)**:
   * Do NOT model unpurchasable experimental nanomaterials. The physical target is standard, commercial **3D QLC/TLC NAND Flash**.
5. **Always Update `PROGRESS.md`**:
   * Whenever you complete a milestone or add a feature, update [`PROGRESS.md`](file:///d:/hyper_ram/PROGRESS.md) so the user can seamlessly resume work in a new chat.

---

## 3. Directory Layout

```
hyper_ram/ (transitioning to zFTL)
├── AGENTS.md               # This file: persistent context for AI pair-programmers
├── PROGRESS.md             # Living roadmap, task tracker, and current phase
├── README.md               # Public project documentation & architectural rationale
├── CMakeLists.txt          # C++20 build configuration
├── include/
│   ├── lz4_compressor.hpp  # 4KB block compression engine interface (LZ4)
│   ├── flash_model.hpp     # Physical NAND flash array model (Blocks, Pages, P/E cycles)
│   └── ftl_controller.hpp  # Flash Translation Layer (LBA-to-PBA mapping, packing, WAF)
├── src/
│   ├── lz4_compressor.cpp  # Compression & decompression implementation
│   ├── flash_model.cpp     # NAND flash wear, program/erase latencies
│   ├── ftl_controller.cpp  # LBA mapping table, compressed block packing logic
│   ├── tests.cpp           # Verification test suite
│   └── main.cpp            # Benchmark runner & WAF telemetry report
├── hdl/                    # Synthesizable Verilog modules (pipelined decompressor)
└── docs/                   # Architectural diagrams & design notes
```

---

## 4. Current Workflow Instructions

Before starting any task:
1. Open [`PROGRESS.md`](file:///d:/hyper_ram/PROGRESS.md).
2. Look at **Active Task** under Section 1.
3. Keep changes incremental, testable with `cmake --build build`, and verify with unit tests.
