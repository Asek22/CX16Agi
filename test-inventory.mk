INVENTORY_TEST_CC ?= cl65
INVENTORY_TEST_HOST_CC ?= cc
INVENTORY_TEST_EMU ?= x16emu
INVENTORY_TEST_ROM ?= rom.bin
INVENTORY_TEST_CASE ?= all
INVENTORY_TEST_ROOT := src/Tests/TestInventory
INVENTORY_TEST_OUT := $(TARGETOBJDIR)/tests/inventory

.PHONY: test-inventory
ifeq ($(INVENTORY_TEST_CASE),all)
test-inventory:
	@set -eu; for selected in inventory $(patsubst $(INVENTORY_TEST_ROOT)/test_%_audit.c,%,$(wildcard $(INVENTORY_TEST_ROOT)/test_*_audit.c)); do \
	  echo "INVENTORY CASE: $$selected"; \
	  $(MAKE) --no-print-directory test-inventory INVENTORY_TEST_CASE=$$selected; \
	done
else
test-inventory:
	@mkdir -p $(INVENTORY_TEST_OUT)
	@set -eu; exec 9>"$(INVENTORY_TEST_OUT)/.suite.lock"; flock 9; \
	case_flags=; if [ "$(INVENTORY_TEST_CASE)" = showobj ]; then case_flags="-D INVENTORY_TEST_SHOWOBJ"; fi; \
	if [ "$(INVENTORY_TEST_CASE)" = views ] || [ "$(INVENTORY_TEST_CASE)" = viewaliases ]; then case_flags="$$case_flags -D INVENTORY_TEST_VIEW_RLE"; fi; \
	case_dir=$$(mktemp -d $(INVENTORY_TEST_OUT)/case.XXXXXX); \
	trap 'if [ -d "$$case_dir" ]; then rm -r -- "$$case_dir"; fi' EXIT HUP INT TERM; \
	timeout_seconds=60; \
	if [ "$(INVENTORY_TEST_CASE)" = views ] || [ "$(INVENTORY_TEST_CASE)" = viewaliases ] || [ "$(INVENTORY_TEST_CASE)" = showobj ]; then timeout_seconds=90; fi; \
	if [ "$(INVENTORY_TEST_CASE)" = fullframe ]; then timeout_seconds=90; fi; \
	if [ "$(INVENTORY_TEST_CASE)" = integration ]; then timeout_seconds=180; fi; \
	mkdir "$$case_dir/objects"; \
	for source in src/*.c; do \
	  name=$$(basename "$$source" .c); \
	  $(INVENTORY_TEST_CC) -t cx16 $(CFLAGS) -D main=agiProductMain $$case_flags -c -o "$$case_dir/objects/$$name.o" "$$source"; \
	done; \
	$(INVENTORY_TEST_CC) -t cx16 $(ASFLAGS) $$case_flags -c -o "$$case_dir/objects/main.o" src/main.s; \
	case_sources="$(INVENTORY_TEST_ROOT)/test_inventory.c"; \
	if [ "$(INVENTORY_TEST_CASE)" != inventory ]; then case_sources="$$case_sources $(INVENTORY_TEST_ROOT)/test_$(INVENTORY_TEST_CASE)_audit.c"; fi; \
	for source in $$case_sources; do \
	  name=$$(basename "$$source" .c); \
	  $(INVENTORY_TEST_CC) -t cx16 $(CFLAGS) -D AUDIT_CASE_$(INVENTORY_TEST_CASE) -c -o "$$case_dir/objects/case_$$name.o" "$$source"; \
	done; \
	$(INVENTORY_TEST_CC) -t cx16 $(ASFLAGS) -c -o "$$case_dir/objects/support.o" $(INVENTORY_TEST_ROOT)/support.s; \
	$(INVENTORY_TEST_CC) -t cx16 -C cx16-bank.cfg -o "$$case_dir/agi.cx16" "$$case_dir/objects/"*.o; \
	if [ "$(INVENTORY_TEST_CASE)" = readerfilename ]; then \
	  $(INVENTORY_TEST_HOST_CC) -std=c99 -Wall -Wextra -o "$(INVENTORY_TEST_OUT)/create_readerfilename_fixtures" "$(INVENTORY_TEST_ROOT)/create_readerfilename_fixtures.c"; \
	  (cd "$$case_dir" && "$(abspath $(INVENTORY_TEST_OUT))/create_readerfilename_fixtures"); \
	fi; \
	rom=$$(realpath "$(INVENTORY_TEST_ROM)"); prg=$$(realpath "$$case_dir/agi.cx16"); \
	emu=$$(command -v "$(INVENTORY_TEST_EMU)"); \
	emu_status=0; \
	(cd "$$case_dir"; SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
	  timeout "$$timeout_seconds" "$$emu" -rom "$$rom" -prg "$$prg" -run -warp -sound none > result.log 2>&1) || emu_status=$$?; \
	cat "$$case_dir/result.log"; \
	echo "INVENTORY EMULATOR EXIT: $$emu_status"; \
	test "$$emu_status" -eq 0; \
	rg -q 'INVENTORY PASS' "$$case_dir/result.log"

endif
