Run `make test-inventory INVENTORY_TEST_CC=/path/to/cl65 INVENTORY_TEST_EMU=/path/to/x16emu INVENTORY_TEST_ROM=/path/to/rom.bin` using the same cc65 and X16 r49 tools as the banked-copy suite.

This permanent C suite builds and links all production C/assembly with the product bank map. Only the production entry point is renamed so the cases can run first. The real product initializer loads its generated bank files, real OBJECT records are created in their own bank, and scripted keys go through the KERNAL keyboard queue. No inventory, renderer, allocator, or input implementation is mocked.

Each run owns a unique directory under `obj/cx16/tests/inventory`. All objects, bank images, emulator files and runtime inputs are created there and removed on success, failure, timeout or interruption. No game files or manually staged fixtures are needed.

The initial regression failed with `INVENTORY FAIL: STATUS MUST SELECT THE HIGHLIGHTED CARRIED OBJECT` against the original `b4Status` stub. Cases now cover selection and returning from View, read-only/empty inventory, missing/invalid objects, dagger/carrot/bucket pixel samples and full 2x expansion, unchanged room bitmap, every saved panel tile, mouse/inspect attribute restoration, and RAM/VRAM allocation ownership. The real game IRQ remains active. VRAM probes mask IRQs around the real cc65 `vpeek`/`vpoke`, so concurrent input/status drawing cannot redirect a probe. Unrelated text rows remain under normal game IRQ control.

Built-in art resolves numeric OBJECT entries by their banked names; it does not assume a particular game's object ordering or substitute in-world cels. Artwork is published only after upload, and close restores attributes and waits for a VSYNC before releasing its bitmap.

Five correctness/performance audit rounds:

| Round | Durable workload / result |
| --- | --- |
| 1: visibility | A real VSYNC observer reproduced queued close erasing art before any visible frame. View now waits for one complete visible frame before accepting close. |
| 2: names | Eight open/cancel cycles with 255 carried objects originally averaged 2,393,786 CPU cycles. Bounded bulk name reads remove per-character bank switches and duplicate object fetches. The suite retains a 1,800,000-cycle ceiling. |
| 3: bounds / paging | Real KERNAL queue input paced by VSYNC selects object 254 across every page; upward navigation at object zero does not wrap. Mixed-case, unterminated bank-edge, out-of-bank and null names are checked. |
| 4: exhaustion | Exhausting every panel-compatible RAM allocation reproduced interpreter termination. A nonfatal allocator entry now cancels inventory without changing other callers' allocation policy. Full 64x64 VRAM exhaustion, reserved-area bounds, release and reuse are checked. |
| 5: VRAM state / speed | Transfers at lengths 0, 1, 7, 8, 9, 31, 32, 71, 72, 73, 79 and 80 preserve both address ports, DCSEL, RAM bank, and incoming IRQ state. The 128-transfer 72-byte workload originally averaged 1,476 cycles; eight-byte unrolling reduces it to 1,031 cycles in the final linked build, with a retained 1,100-cycle ceiling. |

After all fixes, the full-inventory workload averaged 1,091,187 cycles (about 54% below its original baseline). Benchmarks read the X16 emulator's cycle counter; the full-inventory case keeps the real VSYNC/game IRQ active, while the short transfer benchmark masks IRQs to measure CPU cost independently of frame alignment. Final verification also reruns `test-banked-copy` and the product build. These cases validate native production paths and pixel/ownership invariants; they do not replace a manual live-game visual check.

Round 5 also inspected KQ1's actual OBJECT names and reproduced missing bucket artwork: object 11 is `water bucket`, and object 12 is `full bucket` (dagger 1, carrot 3). Both bucket names now map to the built-in bucket art. A self-contained native case creates only those indispensable names and object records, checks all four real object IDs and selecting object 12, and requires no installed game assets.
