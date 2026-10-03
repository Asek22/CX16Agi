Run `make test-inventory INVENTORY_TEST_CC=/path/to/cl65 INVENTORY_TEST_EMU=/path/to/x16emu INVENTORY_TEST_ROM=/path/to/rom.bin` using the same cc65 and X16 r49 tools as the banked-copy suite.

This permanent C suite builds and links all production C/assembly with the product bank map. Only the production entry point is renamed so the cases can run first. The real product initializer loads its generated bank files, real OBJECT records are created in their own bank, and scripted keys go through the KERNAL keyboard queue. No inventory, renderer, allocator, or input implementation is mocked.

Each run owns a unique directory under `obj/cx16/tests/inventory`. All objects, bank images, emulator files and runtime inputs are created there and removed on success, failure, timeout or interruption. No game files or manually staged fixtures are needed.

The initial regression failed with `INVENTORY FAIL: STATUS MUST SELECT THE HIGHLIGHTED CARRIED OBJECT` against the original `b4Status` stub. Cases now cover selection and returning from View, read-only/empty inventory, missing/invalid objects, dagger/carrot/bucket pixel samples and full 2x expansion, unchanged room bitmap, every saved panel tile, mouse/inspect attribute restoration, and RAM/VRAM allocation ownership. The real game IRQ remains active. VRAM probes mask IRQs around the real cc65 `vpeek`/`vpoke`, so concurrent input/status drawing cannot redirect a probe. Unrelated text rows remain under normal game IRQ control.

Built-in art resolves numeric OBJECT entries by their banked names; it does not assume a particular game's object ordering or substitute in-world cels. Artwork is published only after upload, and close restores attributes and waits for a VSYNC before releasing its bitmap.
