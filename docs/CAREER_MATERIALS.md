# Career Application Materials — zFTL Project

*Templates for industry internship applications and postgraduate admissions.*  
*Fill in [BRACKETED] fields with your personal information before sending.*

---

## Document 1: Statement of Purpose (MS / M.Tech Programs)

*Target: IIT M.Tech (VLSI / Embedded Systems / Computer Architecture), or MS abroad (US/EU)*  
*Length: ~700–900 words. Edit to fit each program's specific requirements.*

---

**Statement of Purpose**

My interest in computer architecture was not born in a classroom — it was born in frustration. During my second year of B.Tech in Electronics and Communication Engineering at [YOUR COLLEGE], I watched the SSD on my laptop gradually slow down under heavy academic workload. After running diagnostic tools, I found that the drive — a Micron 2400 512 GB QLC NVMe — had already consumed a significant fraction of its rated 150 TBW write endurance within eighteen months of normal student use. The fundamental question that emerged from this experience became the core of my undergraduate research: can the SSD's own firmware be made smart enough to fix this problem transparently?

This question led me to design and implement **zFTL**, an in-line compression-enabled Flash Translation Layer for QLC NAND flash. The project required me to bridge three fields simultaneously: storage systems architecture, digital hardware design, and low-level systems programming. The core insight is straightforward but architecturally significant — if every 4 KB logical block written by the operating system is compressed using LZ4 before being mapped to a physical NAND page, and two compressed blocks are packed into a single physical page, then the number of flash program operations is halved. The Write Amplification Factor (WAF) drops below 1.0, and a 150 TBW drive effectively lives longer.

I built a cycle-accurate simulator in C++20 comprising three components: a 4 KB LZ4 compression engine, a physical 3D QLC NAND flash model with accurate program/erase latency and P/E cycle tracking, and a full Flash Translation Layer with LBA-to-PBA mapping, sparse-zero filtering, half-page packing, and a greedy garbage collector with correct valid-page migration. On a blended workload representing real laptop OS traffic — 35% zero pages, 40% browser cache, 15% binary code, 10% encrypted media — the simulator measures a WAF of **0.40**, corresponding to a drive lifespan of **375 TBW** versus the 150 TBW baseline — a 2.5× improvement. A verification suite of 20 unit and integration tests covers all components and passes cleanly.

Beyond software, I designed a synthesizable Verilog RTL implementation of the LZ4 hardware decompressor targeting integration into SSD controller ASICs. The module implements a 9-state FSM with AXI4-Stream byte-stream I/O. The 4 KB history buffer synthesizes to a single 36 Kb block RAM on Xilinx 7-series FPGAs. This work gave me hands-on experience with the gap between algorithmic correctness in simulation and the constraints of synthesizable hardware design — a gap I want to close through further research.

Working on this project independently as an undergraduate exposed me to open questions I do not yet have the tools to answer. How does the greedy GC policy perform under adversarial write patterns that concentrate invalid pages unevenly? What is the optimal packing threshold given a realistic distribution of LZ4 compression ratios across OS workloads? Can the hardware decompressor be parallelised to 8 or 16 bytes per cycle without exceeding the BRAM read port bandwidth? These are questions that require the deeper foundation in architecture research methodology that a graduate program provides.

I am applying to [PROGRAM NAME] at [INSTITUTE] because [SPECIFIC REASON — faculty research group, research focus area, etc.]. I am particularly interested in working with [FACULTY NAME] whose work on [TOPIC] directly complements the storage architecture questions I have been exploring. I believe that the technical depth I have developed through zFTL — spanning compression algorithms, NAND flash physics, FTL policy design, and RTL implementation — provides a strong foundation for contributing to research in this area.

My goal after graduate study is to work as a research engineer at a semiconductor or storage company, contributing to the design of storage architectures that are efficient, durable, and accessible. I believe that affordable, long-lasting storage is not merely a convenience but a matter of digital equity — and I want to be one of the engineers who builds it.

[YOUR NAME]  
[YOUR EMAIL] | [YOUR GITHUB URL]

---

## Document 2: Resume / CV Bullets — zFTL Project

*Copy these directly into your resume under a "Projects" or "Research" section.*

---

**zFTL — In-Line Compressed Flash Translation Layer for QLC NAND Flash**  
*Independent Research Project | B.Tech ECE | [YOUR COLLEGE] | 2026*  
[GitHub: github.com/YOUR_USERNAME/hyper_ram]

- Designed and implemented a **cycle-accurate C++20 simulator** for an inline-compression Flash Translation Layer targeting 3D QLC NAND SSDs, achieving a measured **Write Amplification Factor (WAF) of 0.40** on blended OS workloads — a **2.5× endurance improvement** over the Micron 2400 QLC baseline (150 TBW → 375 TBW).
- Engineered a **4 KB LZ4 compression engine** with sparse-zero bypass (no flash write for all-zero pages) and half-page packing (two ≤2 KB compressed blocks per physical page), running at **~8.3 GB/s throughput** (0.47 µs/block).
- Implemented a **physical 3D QLC NAND flash model** in C++ with accurate program (800 µs), erase (3,500 µs), and read (50 µs) latencies, P/E cycle tracking, and NAND overwrite constraints.
- Designed and implemented a **greedy garbage collector** with victim block selection by maximum invalid-page count and correct live-page migration with full LBA remapping — eliminating silent data loss present in naive round-robin GC implementations.
- Built a **20-test verification suite** (rewritten from scratch to test the correct stack) covering the compression engine, NAND model, FTL write/read paths, WAF regression, and GC data integrity — **all 20/20 pass**.
- Designed a **synthesizable Verilog RTL LZ4 decompressor** (`lz4_decompressor_4k.v`) with AXI4-Stream I/O, 9-state FSM, and 4 KB history buffer (infers as BRAM) targeting 250 MHz operation on FPGA.
- **Tools**: C++20 (MSVC, MSVC /O2), CMake 3.20, Verilog (Xilinx XSIM), PowerShell CI, GitHub Actions.

---

## Document 3: Industry Cover Letter Template

*Target: Micron India R&D (Hyderabad) / Samsung SSIR (Bangalore) / Western Digital India / Seagate India*  
*Adapt tone slightly per company — Micron and WD prefer technical depth, Samsung prefers innovation framing.*

---

[YOUR NAME]  
[YOUR EMAIL] | [YOUR PHONE] | [YOUR LINKEDIN / GITHUB]  
[DATE]

Hiring Manager  
[COMPANY NAME] — [LOCATION] Office  

Dear Hiring Manager,

I am a final-year / [YEAR]-year B.Tech student in Electronics and Communication Engineering at [YOUR COLLEGE], writing to apply for the [ROLE NAME — e.g., Firmware Engineering Intern / Storage Systems Intern] position.

My interest in [COMPANY NAME] is direct and specific: your [NVMe SSD product line / QLC NAND controller team / storage firmware division] works on exactly the class of problem I have been building toward independently. My undergraduate research project, zFTL, is an in-line compression-enabled Flash Translation Layer designed to reduce Write Amplification Factor on consumer QLC NAND SSDs. On a cycle-accurate simulator calibrated to real QLC parameters (800 µs program latency, 3,500 µs erase, 500 P/E cycle endurance), zFTL achieves a WAF of 0.40 on a blended OS workload — extending a 150 TBW QLC drive's effective lifespan to 375 TBW through transparent LZ4 compression and half-page packing in the FTL write path.

Building this project required me to operate across the full storage stack: I implemented the LZ4 compression engine, the physical NAND flash model, the FTL with LBA-to-PBA mapping and greedy garbage collection with live-page migration, and a 20-test verification suite. I also designed a synthesizable Verilog RTL LZ4 decompressor with AXI4-Stream I/O, targeted at SSD controller ASIC integration. The full codebase, architecture specification, and test results are publicly available at [YOUR GITHUB URL].

I am applying to [COMPANY NAME] because [SPECIFIC REASON — e.g., "Micron's investment in QLC endurance research, particularly the Adaptive Thermal Management and Media Management features in the 3400 and B47R families, represent exactly the firmware domain I want to contribute to."]. I am confident that my hands-on experience with FTL design, NAND flash physics, and hardware-software co-design makes me a strong fit for your team.

I would welcome the opportunity to discuss my work and how I can contribute. Please find my resume and the project repository attached / linked above.

Thank you for your time.

Sincerely,  
[YOUR NAME]

---

## Document 4: Venues to Submit the Paper

*Ranked by accessibility for undergrad / early-career authors.*

| Venue | Type | Deadline (Typical) | Notes |
|:---|:---|:---|:---|
| **IEEE NVMW** (Non-Volatile Memories Workshop) | Workshop | January–February | Most accessible for students, short papers accepted |
| **USENIX HOTSTORAGE** | Workshop | March–April | Hot Topics in Storage, 2-page position papers accepted |
| **ACM SYSTOR** | Conference | February–March | Storage systems, strong community for FTL research |
| **IEEE NVMSA** (Non-Volatile Memory Systems and Applications) | Symposium | May–June | Very relevant, welcomes simulation-based work |
| **USENIX FAST** | Conference | September | Top-tier, competitive — aim for this after NVMW/SYSTOR |
| **NCC / INDICON** (India) | National Conference | Varies | Good starting point for first publication credit in India |
| **IISc / IIT Technical Symposia** | National | Varies | Strong for UG researchers, respected in India |

**Recommendation**: Submit first to **IEEE NVMW** or **USENIX HotStorage** (short paper, 2–4 pages). Use reviewer feedback to strengthen the paper for **ACM SYSTOR** or **USENIX FAST** in the following cycle.
