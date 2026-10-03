INVENTORY_TEST_CC ?= cl65
INVENTORY_TEST_EMU ?= x16emu
INVENTORY_TEST_ROM ?= rom.bin
INVENTORY_TEST_CASE ?= all
INVENTORY_TEST_ROOT := src/Tests/TestInventory
INVENTORY_TEST_OUT := $(TARGETOBJDIR)/tests/inventory

.PHONY: test-inventory
ifeq ($(INVENTORY_TEST_CASE),all)
test-inventory:
	@set -eu; for selected in inventory $(patsubst $(INVENTORY_TEST_ROOT)/test_%_audit.c,%,$(wildcard $(INVENTORY_TEST_ROOT)/test_*_audit.c)); do \
	  $(MAKE) --no-print-directory test-inventory INVENTORY_TEST_CASE=$$selected; \
	done
else
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
	case_sources="$(INVENTORY_TEST_ROOT)/test_inventory.c"; \
	if [ "$(INVENTORY_TEST_CASE)" != inventory ]; then case_sources="$$case_sources $(INVENTORY_TEST_ROOT)/test_$(INVENTORY_TEST_CASE)_audit.c"; fi; \
	for source in $$case_sources; do \
	  name=$$(basename "$$source" .c); \
	  $(INVENTORY_TEST_CC) -t cx16 $(CFLAGS) -D AUDIT_CASE_$(INVENTORY_TEST_CASE) -c -o "$$case_dir/objects/case_$$name.o" "$$source"; \
	done; \
	$(INVENTORY_TEST_CC) -t cx16 $(ASFLAGS) -c -o "$$case_dir/objects/support.o" $(INVENTORY_TEST_ROOT)/support.s; \
	$(INVENTORY_TEST_CC) -t cx16 -C cx16-bank.cfg -o "$$case_dir/agi.cx16" "$$case_dir/objects/"*.o; \
	rom=$$(realpath "$(INVENTORY_TEST_ROM)"); prg=$$(realpath "$$case_dir/agi.cx16"); \
	emu=$$(command -v "$(INVENTORY_TEST_EMU)"); \
	emu_status=0; \
	(cd "$$case_dir"; SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
	 timeout 60 "$$emu" -rom "$$rom" -prg "$$prg" -run -warp -sound none > result.log 2>&1) || emu_status=$$?; \
	cat "$$case_dir/result.log"; \
	test "$$emu_status" -eq 0; \
	rg -q 'INVENTORY PASS' "$$case_dir/result.log"

endif
