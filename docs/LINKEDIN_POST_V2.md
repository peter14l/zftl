# LinkedIn Post V2 — Personal Hook Angle

> **Why this post is different from V1:**
> V1 was too technical up front and buried the human story. This version leads with emotion and a shocking personal discovery, uses shorter paragraphs, and has a single clear CTA question that invites engineers to comment.
>
> **Best time to post (IST):** Tuesday or Wednesday, 8:00 AM – 9:00 AM or 6:00 PM – 7:00 PM for maximum Indian tech audience reach.
>
> **Image to attach:** A clean screenshot of `.\build\Release\zftl_sim.exe` output — specifically the Blended Real-World row showing WAF = 0.40 and 375 TBW lifespan.

---

## ✍️ Post Text (copy-paste below this line)

---

My 1-year-old laptop already has 91% SSD health left.

At this rate, it'll be dead before I graduate.

I'm a 1st-year ECE student and I wasn't willing to just accept that. So I spent the last few weeks building something about it.

---

Here's the physics problem nobody talks about in class:

Modern budget laptops (mine included) ship with QLC NAND flash — 4 bits per cell, which makes it cheap to manufacture. But it also means the drive is rated for only ~150 TB of total writes before it wears out.

Your OS writes more than you think:
→ Virtual memory paging
→ Browser cache flushing
→ System log rotation

That's 20–40 GB/day on a student workload. Every single write ages the drive.

---

So I built **zFTL** — a compressed Flash Translation Layer that intercepts writes before they hit physical flash.

The idea: LZ4 compression takes < 1 µs per 4 KB block. Flash programming takes ~800 µs per page. If I compress data first, I write fewer physical pages. Fewer writes = less wear.

The measured results surprised even me:

✅ Windows pagefile traffic → WAF 0.67 (−33% wear)
✅ Browser cache writes → WAF 0.50 (−50% wear)
✅ Blended daily laptop profile → **WAF 0.40 (−60% wear)**

At WAF 0.40, a 150 TBW drive effectively behaves like a **375 TBW drive**.

That's the difference between a drive dying in 3 years vs. lasting through your entire degree — and beyond.

---

The full simulator is in C++20 with a synthesizable Verilog RTL decompressor (AXI4-Stream, 250 MHz, dual-port history buffer). 16/16 unit tests pass. The WAF numbers are from real OS workload traces, not synthetic benchmarks.

I'm sharing this not because it's polished — it isn't — but because I think the gap between "student learning embedded systems" and "student understanding the storage layer their OS runs on" is enormous, and nobody talks about it.

If you work in storage systems, SSD firmware, or NAND flash architecture: **what would YOU test next?** Garbage collection pressure? Write cliff behavior under sustained burst loads?

Drop a comment. I genuinely want to learn what I'm missing.

#ECE #StorageSystems #NANDFlash #SSD #ComputerArchitecture #Verilog #DigitalDesign #EngineeringStudent #OpenSource #Hardware

---

## 📸 Recommended Image

Run in PowerShell:
```powershell
.\build\Release\zftl_sim.exe
```

Screenshot the full terminal output — especially the table rows showing:
- `zFTL Measured WAF: 0.40`
- `Effective Micron 2400 Lifespan: 375.32 TBW`

Dark-mode terminal on a black background works best. Crop tightly.
