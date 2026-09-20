# zFTL Architecture Specification

**Project**: zFTL — In-Line Compressed Flash Translation Layer for 3D QLC NAND Flash  
**Version**: 1.0 | **Status**: Simulation-verified, RTL-complete

---

## 1. System Context

Modern consumer SSDs use a **Flash Translation Layer (FTL)** — firmware running on the SSD controller's embedded processor — to translate logical block addresses (LBAs) from the host OS into physical NAND page addresses (PBAs). The FTL also manages garbage collection, wear leveling, and bad block management.

**zFTL** extends a conventional FTL with a transparent inline compression stage. Every 4 KB host write is compressed before being mapped to physical NAND, reducing the number of flash program operations per unit of host data.

```
┌──────────────────────────────────────────────────┐
│                   Host System                    │
│  OS / File System / NVMe Driver                  │
│  Issues 4 KB LBA Read/Write commands             │
└────────────────────┬─────────────────────────────┘
                     │  NVMe PCIe Interface
                     ▼
┌──────────────────────────────────────────────────┐
│              SSD Controller (Phison / SMI)        │
│                                                  │
│  ┌────────────────────────────────────────────┐  │
│  │             zFTL Layer                     │  │
│  │                                            │  │
│  │  ┌──────────────┐   ┌───────────────────┐  │  │
│  │  │ LZ4          │   │  LBA → PBA        │  │  │
│  │  │ Compression  │──▶│  Mapping Table    │  │  │
│  │  │ Engine       │   │  (per-LBA entry)  │  │  │
│  │  └──────────────┘   └────────┬──────────┘  │  │
│  │                              │              │  │
│  │  ┌───────────────────────────▼──────────┐  │  │
│  │  │ Half-Page Packer & Staging Buffer    │  │  │
│  │  │ (pairs two ≤2 KB blocks → 1 page)   │  │  │
│  │  └───────────────────────────┬──────────┘  │  │
│  │                              │              │  │
│  │  ┌───────────────────────────▼──────────┐  │  │
│  │  │ Greedy Garbage Collector              │  │  │
│  │  │ (max-invalid-first victim selection)  │  │  │
│  │  └───────────────────────────┬──────────┘  │  │
│  └──────────────────────────────┼─────────────┘  │
│                                 │                 │
└─────────────────────────────────┼─────────────────┘
                                  │  ONFI / Toggle Interface
                                  ▼
┌──────────────────────────────────────────────────┐
│          Physical 3D QLC NAND Flash Array         │
│  256 blocks × 64 pages × 4 KB = 64 MB (sim pool) │
│  Page Program: 800 µs | Erase: 3,500 µs          │
└──────────────────────────────────────────────────┘
```

---

## 2. Write Path (Detailed)

Every `WriteBlock(lba, src_4kb)` call follows this decision tree:

```
WriteBlock(lba, src_4kb)
        │
        ▼
  Compress(src_4kb)  ← LZ4_compress_default(), max output = 4096 B
        │
        ├── is_zero_block == true?
        │       │
        │       └── YES → Mark lba_table[lba] = SPARSE_ZERO
        │                  No flash write. WAF contribution = 0.
        │
        ├── compressed_size ≤ 2048?  (fits in a half-page slot)
        │       │
        │       ├── staged_half_ empty?
        │       │       └── YES → Stage this block. Wait for a partner.
        │       │
        │       └── staged_half_ has a partner?
        │               └── YES → Pack both into one 4 KB flash page.
        │                          ProgramPage(block, page, [half_A | half_B])
        │                          Map both LBAs: sub_slot 0 and sub_slot 1.
        │                          WAF contribution = 0.5 per block.
        │
        └── compressed_size > 2048  (incompressible — store raw)
                │
                └── Flush any staged half (FlushStagedWrites())
                    ProgramPage(block, page, raw_4kb)
                    Map lba: RAW_FULL_PAGE.
                    WAF contribution = 1.0.
```

---

## 3. Read Path

```
ReadBlock(lba, dest_4kb)
        │
        ├── lba in staged_half_?  → Decompress staged buffer → return
        │
        ├── lba_table[lba].state == UNMAPPED     → memset(dest, 0) → return
        ├── lba_table[lba].state == SPARSE_ZERO  → memset(dest, 0) → return
        │
        ├── lba_table[lba].state == RAW_FULL_PAGE
        │       └── ReadPage(block, page) → memcpy → return
        │
        └── lba_table[lba].state == COMPRESSED_HALF_PAGE
                └── ReadPage(block, page, flash_page[4096])
                    offset = (sub_slot == 0) ? 0 : 2048
                    LZ4_decompress_safe(flash_page + offset, dest, comp_size, 4096)
```

---

## 4. LBA Mapping Table

Each entry in `lba_table_[lba]` is a `MappingEntry`:

| Field | Type | Description |
|:---|:---|:---|
| `state` | `MappingState` | UNMAPPED / SPARSE_ZERO / COMPRESSED_HALF_PAGE / RAW_FULL_PAGE |
| `block_idx` | `uint32_t` | Physical NAND block index |
| `page_idx` | `uint32_t` | Physical page within block |
| `sub_slot` | `uint8_t` | 0 = first 2 KB half, 1 = second 2 KB half |
| `compressed_size` | `uint16_t` | Bytes occupied in the flash page |

---

## 5. Greedy Garbage Collection

**Trigger**: `active_page_idx_ >= pages_per_block` (active block is full).

**Algorithm**:

```
Phase 1 — Victim Selection (Greedy / Cost-Benefit):
  victim = argmax_b { invalid_page_count[b] }  (skip reserve block)
  if max_invalid == 0: advance to reserve block, rotate reserve pointer, return.

Phase 2 — Valid-Page Migration:
  reserve_write_page = 0
  for p in 0..pages_per_block:
    if not IsPageValid(victim, p): clear owner record, continue
    tmp = ReadPage(victim, p)
    ProgramPage(reserve_block, reserve_write_page, tmp)
    for each (lba, sub_slot) in block_page_owners[victim][p]:
      lba_table[lba].{block_idx, page_idx} = (reserve_block, reserve_write_page)
    block_page_owners[reserve_block][reserve_write_page] = move(owners)
    reserve_write_page++

Phase 3 — Erase & Rotate:
  EraseBlock(victim)
  active_block = victim, active_page = 0
  rotate reserve_block_idx
```

**Why greedy?** The block with the most invalid pages frees the most space per erase, minimising total erase operations and therefore minimising write amplification from GC itself.

---

## 6. Hardware RTL: LZ4 Decompressor (`lz4_decompressor_4k.v`)

### Interface

| Signal | Direction | Description |
|:---|:---:|:---|
| `clk`, `rst_n` | In | Clock (250 MHz target), active-low reset |
| `start` | In | Pulse to begin decompression of a new block |
| `busy`, `done` | Out | Block processing status |
| `error_flag` | Out | Asserted on malformed LZ4 stream |
| `decompressed_bytes[12:0]` | Out | Count of output bytes produced |
| `s_axis_tdata/tvalid/tready/tlast` | AXI4-S Slave | Compressed byte input stream |
| `m_axis_tdata/tvalid/tready/tlast` | AXI4-S Master | Decompressed byte output stream |

### State Machine

```
ST_IDLE → ST_TOKEN → ST_EXT_LIT → ST_LITERAL → ST_OFFSET_L
                                                     │
                                              ST_OFFSET_H → ST_EXT_MATCH → ST_MATCH_COPY
                                                                                  │
                                                                              ST_DONE → ST_IDLE
```

| State | Action |
|:---|:---|
| `ST_TOKEN` | Parse token byte: upper nibble = literal length, lower nibble = match length |
| `ST_EXT_LIT` | Accumulate extended literal length (0xFF continuation bytes) |
| `ST_LITERAL` | Stream literal bytes to output & write to `history_ram[]` |
| `ST_OFFSET_L/H` | Read 2-byte little-endian match offset |
| `ST_EXT_MATCH` | Accumulate extended match length (0xFF continuation bytes) |
| `ST_MATCH_COPY` | Copy `match_len` bytes from `history_ram[write_ptr - offset]` to output |
| `ST_DONE` | Assert `done`, clear `busy`, return to `ST_IDLE` |

### Resource Estimates (Xilinx 7-Series target)

| Resource | Estimate | Notes |
|:---|:---:|:---|
| BRAM (4 KB history RAM) | 1 × RAMB36 | Inferred from `history_ram[0:4095]` |
| LUTs | ~350–500 | State machine + datapath |
| FFs | ~120–180 | Pipeline registers |
| Target Fmax | ~200–250 MHz | Pending timing analysis via Vivado |

---

## 7. Slot Quantization

After compression, the output size is mapped to a physical slot:

| Compressed Size | Slot | Physical Footprint | Host Blocks per Page |
|:---|:---:|:---:|:---:|
| 0 bytes (all-zero) | `SPARSE_ZERO` | 0 KB (no write) | ∞ |
| 1 – 1024 bytes | `SLOT_1KB` | 1 KB | 4× |
| 1025 – 2048 bytes | `SLOT_2KB` | 2 KB | 2× |
| 2049 – 3072 bytes | `SLOT_3KB` | 3 KB | ~1.3× |
| 3073 – 4096 bytes | `FULL_4KB` | 4 KB | 1× (no benefit) |

> The current FTL implementation uses the **SLOT_2KB boundary** as the half-page packing threshold. SLOT_1KB packing (4 blocks per page) is an identified future improvement.

---

## 8. WAF Definition

$$\text{WAF} = \frac{\text{Total Physical Bytes Written to Flash}}{\text{Total Logical Bytes Written by Host}}$$

- WAF < 1.0 → compression is saving flash writes (desired)
- WAF = 1.0 → no net benefit (incompressible data)
- WAF > 1.0 → write amplification from GC exceeds compression savings (bad — not observed in current sim)

---

## 9. Known Limitations & Future Work

| Limitation | Impact | Proposed Solution |
|:---|:---|:---|
| Single-byte-per-cycle HDL decompressor | Low RTL throughput (~200 MB/s) | Unroll to 8-byte parallel datapath |
| `match_read_addr` combinational wire | Timing risk above ~200 MHz | Register the address one cycle early |
| SLOT_1KB packing not implemented in FTL | Misses 4× packing opportunity | Add 1 KB staging buffer alongside 2 KB |
| Simulated flash pool only 64 MB | Not representative of 512 GB drives | Scale to full capacity simulation |
| GC wear leveling not implemented | Uneven block wear at high utilisation | Add age-based victim tie-breaking |
| LZ4 hardware compressor absent | Only decompressor in HDL | Design pipelined LZ4 encoder RTL |
