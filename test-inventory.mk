INVENTORY_TEST_CC ?= cl65
INVENTORY_TEST_EMU ?= x16emu
INVENTORY_TEST_ROM ?= rom.bin
INVENTORY_TEST_ROOT := src/Tests/TestInventory
INVENTORY_TEST_OUT := $(TARGETOBJDIR)/tests/inventory

.PHONY: test-inventory
test-inventory:
	@mkdir -p $(INVENTORY_TEST_OUT)
	@set -eu; case_dir=$$(mktemp -d $(INVENTORY_TEST_OUT)/case.XXXXXX); \
	trap 'rm -rf "$$case_dir"' EXIT HUP INT TERM; \
	mkdir "$$case_dir/objects"; \
	for source in src/*.c; do \
	  name=$$(basename "$$source" .c); \
	  $(INVENTORY_TEST_CC) -t cx16 $(CFLAGS) -D main=agiProductMain -c -o "$$case_dir/objects/$$name.o" "$$source"; \
	done; \
	$(INVENTORY_TEST_CC) -t cx16 $(ASFLAGS) -c -o "$$case_dir/objects/main.o" src/main.s; \
	$(INVENTORY_TEST_CC) -t cx16 $(CFLAGS) -c -o "$$case_dir/objects/cases.o" $(INVENTORY_TEST_ROOT)/test_inventory.c; \
	$(INVENTORY_TEST_CC) -t cx16 $(ASFLAGS) -c -o "$$case_dir/objects/support.o" $(INVENTORY_TEST_ROOT)/support.s; \
	$(INVENTORY_TEST_CC) -t cx16 -C cx16-bank.cfg -o "$$case_dir/agi.cx16" "$$case_dir/objects/"*.o; \
	rom=$$(realpath "$(INVENTORY_TEST_ROM)"); prg=$$(realpath "$$case_dir/agi.cx16"); \
	emu=$$(command -v "$(INVENTORY_TEST_EMU)"); \
	(cd "$$case_dir"; SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
	 timeout 60 "$$emu" -rom "$$rom" -prg "$$prg" -run -warp -sound none > result.log 2>&1); \
	cat "$$case_dir/result.log"; \
	rg -q 'INVENTORY PASS' "$$case_dir/result.log"
