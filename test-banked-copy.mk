# Native C cases execute the production helper on a real banked X16 emulator.
BANK_TEST_CC ?= cl65
BANK_TEST_EMU ?= x16emu
BANK_TEST_ROM ?= rom.bin
BANK_TEST_CFLAGS ?= $(CFLAGS)
BANK_TEST_ROOT := src/Tests/TestBankedCopy
BANK_TEST_OUT := $(TARGETOBJDIR)/tests/banked-copy

.PHONY: test-banked-copy
test-banked-copy:
	@mkdir -p $(BANK_TEST_OUT)
	@set -eu; case_dir=$$(mktemp -d $(BANK_TEST_OUT)/case.XXXXXX); \
	trap 'if [ -d "$$case_dir" ]; then rm -r -- "$$case_dir"; fi' EXIT HUP INT TERM; \
	$(BANK_TEST_CC) -t cx16 $(BANK_TEST_CFLAGS) -c -o "$$case_dir/helpers.o" src/helpers.c; \
	$(BANK_TEST_CC) -t cx16 $(BANK_TEST_CFLAGS) -c -o "$$case_dir/memory.o" src/memoryManager.c; \
	$(BANK_TEST_CC) -t cx16 $(BANK_TEST_CFLAGS) -c -o "$$case_dir/test.o" $(BANK_TEST_ROOT)/test_banked_copy.c; \
	$(BANK_TEST_CC) -t cx16 -c -o "$$case_dir/support.o" $(BANK_TEST_ROOT)/support.s; \
	$(BANK_TEST_CC) -t cx16 -c -o "$$case_dir/copy.o" src/bankedCopy.s; \
	$(BANK_TEST_CC) -t cx16 -C $(BANK_TEST_ROOT)/test.cfg -o "$$case_dir/test.prg" "$$case_dir/"*.o; \
	rom=$$(realpath "$(BANK_TEST_ROM)"); prg=$$(realpath "$$case_dir/test.prg"); \
	emu=$$(command -v "$(BANK_TEST_EMU)"); \
	(cd "$$case_dir"; SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
	 timeout 60 "$$emu" -rom "$$rom" -prg "$$prg" -run -warp -via2 -sound none > result.log 2>&1); \
	cat "$$case_dir/result.log"; \
	rg -q 'BANKED COPY PASS' "$$case_dir/result.log"
