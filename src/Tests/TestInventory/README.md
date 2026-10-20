Run `make test-inventory INVENTORY_TEST_CC=/path/to/cl65 INVENTORY_TEST_EMU=/path/to/x16emu INVENTORY_TEST_ROM=/path/to/rom.bin` using the same cc65 and X16 r49 tools as the banked-copy suite.

This permanent C suite builds and links all production C/assembly with the product bank map. Only the production entry point is renamed so the cases can run first. The real product initializer loads its generated bank files, real OBJECT records are created in their own bank, and scripted keys go through the KERNAL keyboard queue. No inventory, renderer, allocator, or input implementation is mocked.

Each run owns a unique directory under `obj/cx16/tests/inventory`. All objects, bank images, emulator files and runtime inputs are created there and removed on success, failure, timeout or interruption. No game files or manually staged fixtures are needed.

The initial regression failed with `INVENTORY FAIL: STATUS MUST SELECT THE HIGHLIGHTED CARRIED OBJECT` against the original `b4Status` stub. Cases now cover selection and returning from View, read-only/empty inventory, missing/invalid objects, dagger/carrot/bucket pixel samples and full 2x expansion, unchanged room bitmap, every saved panel tile, mouse/inspect attribute restoration, and RAM/VRAM allocation ownership. The real game IRQ remains active. VRAM probes mask IRQs around the real cc65 `vpeek`/`vpoke`, so concurrent input/status drawing cannot redirect a probe. Unrelated text rows remain under normal game IRQ control.

Built-in art resolves numeric OBJECT entries by their banked names; it does not assume a particular game's object ordering or substitute in-world cels. Artwork is published only after upload, and close restores attributes and waits for a VSYNC before releasing its bitmap.

## Current render and script batch

`views` and `viewaliases` cover VIEW RLE rejection before assembly rendering plus valid mirrored, transparent and aliased data. The 12-loop/96-cel workload measures 3,818,905 average and 3,826,012 worst cycles, against its retained 18,261,833 average and 18,266,971 worst baseline (`obj/cx16/tests/inventory/root-views-review.log`). Small VIEWs also load with the medium and large allocation pools exhausted; validation allocates its structure map according to resource size.

`showobj` and `textoverlay` check inspect rendering and exact save-under restoration, including palette, text, video state and allocation failure. The retained short-description workload saves/restores 1,280 bytes in 65,681 cycles, versus 8,192 bytes and 430,135 cycles before row-limited transfers (`root-showobj-review.log`). `scriptstream` and `scriptstrings` exercise interpreter branches and string paths, including a stale high byte corrupting a GOTO displacement.

`fullframe` measures the production interpreter tick with active sound, mirrored and split pixels, and IRQ gaps: 24,717,831 average and 24,718,363 worst cycles against its 24,807,573 baseline (`root-fullframe-review.log`). The largest observed VSYNC entry gap is 134,451 cycles; the interpreter/picture phase dominates this repeatable workload. `integration` runs 20 generated production-room sequences through room and animation ticks, inventory inspect, dialog and sound replacement, checking warm RAM/VRAM ownership; its 40 measured ticks average 3,450,161 and peak at 3,452,998 cycles. These native generated regressions do not replace a manual KQ1 playthrough.

Final verification passed all 30 inventory images (`obj/cx16/tests/inventory/root-full-matrix-complete.log`), the banked-copy suite and the product build. Test helpers were relocated within the existing fixed-memory and bank limits; no assertions or product linker limits were relaxed. Both suites also cleaned their unique fixtures after deliberately failing compiler invocations. Final inspection found no `case.*` directories or leftover runtime inputs.

The related palette audit (`render20`) reproduced `DISTINCT TEXT COLOR PAIRS SHARED A PALETTE OWNER`: foreground/background pairs `(1,1)` and `(2,0)` collided because addition bound more tightly than the original shift expression. The cache now packs the two colour nibbles explicitly. The permanent case checks distinct owners, actual VRAM colours and reuse of an existing pair, restoring the palette and current text colours afterward.

The subsequent rerun exposed insufficient wall-time budgets: the unchanged full-frame case repeatedly exceeded 90 seconds, then passed in 105.47 seconds with a 180-second allowance (`luna-fullframe-180.log` and `luna-fullframe-180.time`). Its worst tick remained 24,718,367 guest cycles. The unchanged inventory case also exceeded its old 60-second cap, passing in 84.35 seconds (`luna-inventory-180.log` and `luna-inventory-180.time`). Every image runs that common inventory prelude, so the runner now permits 180 seconds per image. The 30,000,000-cycle tick ceiling, IRQ-gap limits and pixel assertions are unchanged.

The related review's final matrix passed all 30 images (`obj/cx16/tests/inventory/root-related-audit-matrix-180.log`), including the palette regression, backbuffer stress, full-frame benchmark, integration sequence, row-limited overlays and malformed RLE cases. The product build and banked-copy suite also passed. Deliberately failing compiler invocations returned failure in both suites and left no case directories; final inspection also found no leftover runtime inputs or product-root fixtures.

Rerun a focused case by replacing `fullframe` in this command with `views`, `viewaliases`, `showobj`, `textoverlay`, `scriptstream`, `scriptstrings`, `render20`, or `integration`:

```sh
make test-inventory INVENTORY_TEST_CASE=fullframe \
  INVENTORY_TEST_CC=/home/arbitrator/Documents/1-Projects/CX16Agi/.tools/cc65-latest/bin/cl65 \
  INVENTORY_TEST_EMU=/home/arbitrator/Documents/1-Projects/CX16Agi-NEEDS-MERGING/cx16emu/x16emu \
  INVENTORY_TEST_ROM=/home/arbitrator/Documents/1-Projects/CX16Agi-NEEDS-MERGING/cx16emu/rom.bin
```

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

The sibling resource audit uses the same permanent C suite and production bank map. `make test-inventory` now runs the original inventory cases and each `test_*_audit.c` group in separate native images. Each image has its own unique fixture directory under the same output root and cleans it on success, failure, timeout and interruption. Separate images avoid exhausting the target's limited low RAM with unrelated test buffers. The emulator result is printed even on timeout before cleanup.

For a focused rerun, add `INVENTORY_TEST_CASE=sound`, `metadata`, `allocator`, `input`, `views`, `viewaliases`, `showobj`, `textoverlay`, `fullframe`, or `integration` to the command above. Each group still runs the original inventory pixel, ownership and performance checks first.

| Group | Reproduced failure and resulting behavior |
| --- | --- |
| Sound | 166-byte channel refill used the 500-byte golden destination; moving cursors also moved refill boundaries. Generated sound growth omitted preceding channels and kept freed pointer/bank metadata, and resize/string-copy helpers returned the wrong pointers. White-noise advancement dereferenced zero page instead of the note duration. Native cases check real precomputation, refill canaries, cross-bank resize, complete flushes and retired allocation reuse. |
| Metadata | Cached payload cleanup freed an unrelated bank; a full cache recursed indefinitely through freed metadata. Native allocator snapshots now verify owning-bank release and bounded switching of a full cache. |
| Allocator | Failed bulk allocations leaked their successful prefix; zero/oversized counts wrapped the output. Native cases verify rollback, unchanged output on invalid counts, bank restoration and the valid 64-result boundary. |
| Input IRQ | The sound handler left X at eight, so the hidden input line retained its final eight characters. A real VSYNC now clears all 40 cells while preserving the adjacent tile. |
| View headers | The loader cached pointer-to-pointer bytes, skipped offsets for eight through fourteen loops, and maximum-size headers overwrote the neighboring buffer and sprite-transfer code. Native cases check cached structs, all offsets, 255-loop and 255-cel bounds, and executable-code canaries. |
| Legacy object overlay | The viewer discarded an already-loaded view, left attributes referencing released VRAM, and erased existing text. Native cases check rendered RLE pixels across distinct banks, a visible frame and consumed close event, full text/attribute/video restoration, retained caller ownership, and RAM/VRAM exhaustion. Debug STP instructions were removed from the legacy path. |
| VIEW RLE bounds and aliases | Before assembly rendering, the loader now rejects truncated rows, missing terminators, zero runs, over-wide rows, overlapping structure headers, and data beyond the owned resource bank. Tests retain transparent rows, mirrored pixels, 255 aliases to a valid loop, valid 1,024/2,048/8,000-byte resources, a small-map success with medium/large pools exhausted, and a true map-allocation failure. The 12-loop/96-cel load workload retains its original 18,261,833-average and 18,266,971-worst-cycle baseline. |
| Production full tick (`fullframe`) | A generated native fixture runs the shared interpreter tick with room drawing, four animated views, a mirrored cel, split sprites, live PSG sound, and the real game IRQ. It checks room and sprite pixels, reports full-tick and phase average/worst cycles, captures IRQ entry gaps, retains a 24,807,573-cycle baseline and 30,000,000-cycle ceiling, and names the next assembly candidate from measured phase costs. |
| Warm room integration (`integration`) | Twenty script-driven room changes each run the production room and animation ticks, inventory selection/inspect, a real dialog, and active sound replacement. The suite verifies the full room bitmap and warm RAM/VRAM state after each overlay, stale sprite-reference retirement, reserved mouse/inspect sprites, sound IRQ progress, ownership cleanup, and average/worst frame costs. |

These checks use actual production routines, real banked RAM/VRAM and the game IRQ; no game assets or manually prepared files are required. The banked-copy suite and product build remain part of final verification. Interactive visual and audio playthroughs remain separate from these deterministic native cases.

The next audit covers twenty additional perspectives. Confirmed defects were reproduced before production changes, then fixed in small commits. Cases also retain valid boundary behavior so rejection guards cannot silently disable ordinary operations.

| Round | Perspective, durable case and result |
| --- | --- |
| 1 | RAM ownership (`resource20`): interior, out-of-window and unowned-bank frees released unrelated allocations; zero/negative sizes consumed segments. Validate ownership, alignment and size before modifying allocation flags. |
| 2 | Resource integrity (`resource20`): truncated reads copied stale work-area bytes into live resources. Reject incomplete headers/payloads, release failed allocations, and check complete multichunk reads and valid empty message sections. Runtime VOL fixtures belong to the suite's unique directory. |
| 3 | Helper ABI (`resource20`, `input`): banked formatting ignored its format/variadic arguments and did not return a result; the declared controller getter had no exported C symbol. Real native calls check formatted bytes, return values, bank restoration and controller boundary bits. |
| 4 | Cel dimensions (`resource20`): doubling byte widths 128 and above wrapped the sprite-slot calculation. Real VOL loading checks widths 127, 128, 160 and 255 with an unsigned intermediate. |
| 5 | Metadata arithmetic (`resource20`): four slots times 64 cels wrapped the address stride to zero, overlapping later loops and backbuffers. Native banked metadata checks the complete 256-address stride. |
| 6 | Sprite pool and publication (`render20`, `blit20`): all sixteen allocation sizes pass exhaustion, exact release and reuse. Room publication now rejects more than six segments per object and capacity beyond 36 seven-byte records plus the two-byte terminator in the physical 256-byte IRQ buffer. |
| 7 | Split rendering (`render20`, `upload20`, `priority20`): row pointers used pixel widths, padding ignored row offsets, tall/wide rounding wrapped, and dense RLE overflowed the fixed workspace. Segment uploads used the complete cel height. Cases check 8-by-4 split capacity, bounded failure, a live allocation between VRAM spans, every interior pixel, priority at each animated column, and mirrored partial-column positions/pixels. |
| 8 | Picture borders (`render20`): a brush at x=0 underflowed its byte coordinate and drew nothing. Signed clipped traversal preserves left/right/bottom pixels without writing row 168. |
| 9 | Palette ownership (`render20`): a full pool rejected an existing owner, and reset cleared only half the owner words. Full-pool reuse, failure for a new owner, and complete reset pass. |
| 10 | Loader lifecycle (`render20`): repeated display/dismiss calls preserve room bitmap pixels and return to the normal display. This loader uses the text layer; it owns no disposable RAM/VRAM allocation. |
| 11 | Interrupt context (`irq20`, `upload20`): disabled sound left IRQs masked. Enabled sprite uploads also lost interior pixels when the real VSYNC handler changed VERA ports and shared scratch. The handler now preserves cc65 registers, render/interpreter scratch, both address ports, control and FX cache-write state; IRQ-owned sound note pointers continue advancing. The same full-pixel workload passes with IRQs enabled. |
| 12 | Sound lifetime (`irq20`): stop signalled an unrelated completion flag, and discard freed a playing resource before retiring IRQ readers. Real PSG playback, null/repeated discard, retired-bank reuse and silence on the following IRQ pass. |
| 13 | Input row bounds (`irq20`): forty characters plus prompt/cursor overwrote adjacent tiles. Reserve the prompt tile and keep the cursor within the row; neighboring tile sentinels survive VSYNC. |
| 14 | Controller/menu dispatch (`irq20`, `input`): high PETSCII read beyond its table, and disabled menu ENTER activated its controller. Invalid keys remain inactive; enabled actions and the native getter still work. |
| 15 | Text bounds and source ownership (`irq20`): explicit newline wrapping lost line boundaries/widths; truncation modified banked source and overflowed destination; border expansion exceeded its output. Native cases check all three paths with source and destination sentinels. |
| 16 | Parser normalization and capacity (`runtime20`, `interpreter20`): synonym IDs were truncated to a byte, punctuation was restored after cleaning, short words failed before a space, and twenty one-letter words overwrote the ten-word arrays and input buffer. Normalize consistently, trim shortened ranges, bound recognized words and reject invalid getter indices. |
| 17 | Dictionary lifetime/search (`runtime20`, `interpreter20`): discard leaked the word-pointer and synonym allocations. Release all three owners and reset them for repeated discard. Empty and below-first dictionary searches also retain explicit missing-word checks. |
| 18 | Interpreter stream/state (`interpreter20`): `said(..., REST_OF_LINE)` returned false and skipped the wrong byte count, stale words could satisfy tests beyond current input, and argument bytes wrapped at 128 words. Actual `executeLogic` scripts check matching, rejection, following statements and wide argument skips. |
| 19 | Movement assets (`runtime20`): follow-ego used view zero's width instead of ego's current view. Distinct real loaded views verify center collision, direction and completion flag. |
| 20 | Cold-render performance (`blit20`): first render split every cel in the loop even though only the selected cel was needed. Retain the two-cel workload and its 226,335-cycle baseline; selected-cel rendering measures about 120,000 cycles with a 180,000-cycle ceiling. Unselected art must remain unallocated. |

`make test-inventory` discovers every `test_*_audit.c` group and runs each in a separate native image, plus the inventory image. Focused reruns use `INVENTORY_TEST_CASE=resource20`, `render20`, `irq20`, `interpreter20`, `runtime20`, `blit20`, `upload20`, or `priority20`; the original cases remain available too. Separate images and bounded test scratch accommodate the actual low-RAM map without changing product linker limits. Every image first runs the original inventory pixel/ownership/performance checks.

The IRQ-enabled split-upload workload retains its 106,823-cycle pre-fix baseline and reports the average of eight production transfers; pixel verification occurs outside the timed interval. With interrupt context preservation it measures about 98,000 cycles. Existing full-inventory and 72-byte transfer ceilings remain 1,800,000 and 1,100 cycles. Final verification reruns this complete suite, `make test-banked-copy`, and `make PROGRAM=obj/cx16/agi CC=/path/to/cl65`. Successful, failing and timed-out runs use the same cleanup trap; final checks also verify a forced build failure leaves no fixture directories.

The subsequent campaign used 45 GPT-6 Luna Max audits, one for each previously fixed issue, to search for similar defects elsewhere. Confirmed findings have permanent C regressions and separate production commits. Additional groups cover directory, OBJECT, dictionary and debug-file bounds; menu storage and incoming IRQ masks; textbox layout and temporary save-under restoration; script operands, string copies and KERNAL input; and graphics entry points. Existing groups also check metadata allocation overflow, active cache aliases, signed picture clipping, high cel indexes, selected-cel overlay sizing, fresh-view allocation rollback, sound refill boundaries and sound-pool capacity. Inventory art and name-only details must remain visible for a complete frame before queued input closes them.

Three repeatable workloads retain measured baselines and cycle ceilings:

| Workload | Baseline cycles | Optimized cycles |
| --- | ---: | ---: |
| Split upload, IRQ masked, 64-call average (`upload20`) | 90,827 | 86,644 |
| One-slot cel 254 setup (`blit20`) | 22,224 | 16,459 |
| Priority-table population, eight-call average (`priority20`) | 1,712,466 | 994,904 |

The runner requires `flock` and serializes compilation and emulator execution within this output tree. The `readerfilename` group also builds a permanent C fixture helper with `INVENTORY_TEST_HOST_CC` (default `cc`), because target file creation cannot create the built-in long sprite-debug filename. Its files stay inside the unique case directory. That case verifies the actual long-name payload and rejects names beyond the 251-byte filename limit before appending `,S,R`. This emulator's HostFS cannot supply the payload at the full 255-byte SETNAM boundary; the raw boundary probe reports that limitation explicitly, while oversized-name rejection remains unconditional.

Rerun any new group with the command at the top and `INVENTORY_TEST_CASE=readerfilename`, `readerdir`, `readerobject`, `readerwords`, `readerdebug`, `menustorage`, `textlayout`, `textoverlay`, `scriptstrings`, `scriptstream`, `graphicsapi`, `fullframe`, or `integration`. These native checks cover pixels, ownership, interpreter continuation and CPU cost; manual game visuals and audible playback remain outside their scope.

The focused `priority20` case also runs a five-cel 42×10 castle-flag script with the captured channel-1 note pointer set to `0xACF6`; it checks split-sprite publication, pixels, and mouse-slot preservation.

Final campaign validation passed all 26 inventory images, the banked-copy suite and the product build. IRQ, menu-storage and sound-pool test helpers were placed in existing banks to keep every image within the unchanged linker limits. Deliberate compiler failures in both suites also removed their fixture directories; final inspection found no `case.*` directories or product-root runtime inputs.

Backbuffer retirement adds `test_backbuffer_audit.c`. The original resize case failed with `REPEATED BACK-BUFFER RESIZE LEAKED VRAM OWNERSHIP`. The retained case alternates small and large loops for 24 pairs, crosses the retirement ring's wrap point, and checks RAM/VRAM ownership after every pair. It uses real `agiBlit`, sprite IRQ publication and normal `b9AnimateObjects` collection. Additional cases cover two objects, a later publication before collection, split spans with different allocation geometry and a live neighboring canary, refusal to resize an unpublished buffer, partial allocation rollback and retry, and repeated room resets with pending retirements.

Old spans retain their original addresses, dimensions and slot count until the IRQ finishes applying replacement attributes. The IRQ acknowledges a bounded queue prefix; main code releases that prefix. Room reset disables engine sprite attributes and discards pending updates before freeing ownership, including active slots after a disabled gap, while retaining the mouse and reserved inspect attributes. The existing `metadata` case also caught a restored-view write through the payload bank; the shared metadata write now selects its owning bank explicitly.

The eight-call steady-blit workload measures 26,569 cycles per call in the final linked image and retains a 30,000-cycle ceiling. Timing masks IRQs; publication, pixels and ownership are checked outside the timed interval. Full validation passes 27 inventory images, `test-banked-copy` and the product build within the unchanged memory limits. Success and deliberate compiler failure leave no fixture directories. These native checks do not replace a manual game playthrough.

Focused rerun using the verified local tools:

```sh
make test-inventory INVENTORY_TEST_CASE=backbuffer \
  INVENTORY_TEST_CC=/home/arbitrator/Documents/1-Projects/CX16Agi/.tools/cc65-latest/bin/cl65 \
  INVENTORY_TEST_EMU=/home/arbitrator/Documents/1-Projects/CX16Agi-NEEDS-MERGING/cx16emu/x16emu \
  INVENTORY_TEST_ROM=/home/arbitrator/Documents/1-Projects/CX16Agi-NEEDS-MERGING/cx16emu/rom.bin
```

Use `INVENTORY_TEST_CASE=metadata` for the cache-state regression, or omit the case selector to run the complete matrix. Both groups create and clean their inputs under `obj/cx16/tests/inventory`.
