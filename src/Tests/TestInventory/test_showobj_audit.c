#include "audit.h"
#include "../../showObj.h"
#include "../../inventory.h"
#include "../../textLayer.h"
#include <cbm.h>
#include <stdio.h>
#include <string.h>

extern byte allocationArray[], bDSpriteAllocTable[];
extern unsigned long inventoryAddress;
extern byte inventoryBuffer[80], inventoryLength, inventoryWrite;
extern void inventoryTransfer(void);
extern void testPutKey(byte key);
extern void testDisableInterrupts(void), testEnableInterrupts(void);
extern byte testStatus(void);
extern unsigned testVisibleFrames;
extern long b3PaletteAddress;
extern byte b3PaletteNumber;
extern char* currentTextBuffer;
extern unsigned long displayTextAddressToCopyTo;
extern void setLoadedLoop(View* view, Loop* loop, byte number);
extern void setLoadedCel(Loop* loop, Cel* cel, byte number);
#pragma wrapped-call (push, trampoline, SPRITE_MEMORY_MANAGER_BANK)
extern void bDResetSpriteTablePointer(void);
#pragma wrapped-call (pop)
#pragma wrapped-call (push, trampoline, 0x09)
void showObjCheckTextOverlay(void);
void showObjSeedTextOverlay(void);
void showObjCheckTextRow(void);
void showObjCheckTextContext(byte* saved, byte save);
#pragma wrapped-call (pop)
#pragma wrapped-call (push, trampoline, 7)
void showObjExhaustionCases(void);
#pragma wrapped-call (pop)
#pragma wrapped-call (push, trampoline, 0x0c)
static void showObjWideCelCase(View* view, Loop* loop, Cel* cel, VeraSpriteAddress neighbor);
#pragma wrapped-call (pop)
#pragma wrapped-call (push, trampoline, 0x12)
static void showObjSaveUnderApiCase(void);
#pragma wrapped-call (pop)
#pragma wrapped-call (push, trampoline, 0x0f)
void showObjTallCelCase(View* view, Loop* loop, Cel* cel, byte height);
#pragma wrapped-call (pop)
#pragma wrapped-call (push, trampoline, 0x09)
static void showObjTallAuditStages(View* view, Loop* loop, Cel* cel);
#pragma wrapped-call (pop)
#pragma wrapped-call (push, trampoline, CONTROLLER_BANK)
void showObjAuditCasesInBank(VeraSpriteAddress selectedAddress, VeraSpriteAddress neighbor);
#pragma wrapped-call (pop)
#pragma wrapped-call (push, trampoline, 0x0e)
static void showObjPrepareVramSlots(VeraSpriteAddress* selectedAddress, VeraSpriteAddress* neighbor);
#pragma wrapped-call (pop)
void showObjFreshViewCases(void);
#pragma wrapped-call (push, trampoline, CONTROLLER_BANK)
void showObjPrepareViewExhaustion(byte freeTiny);
#pragma wrapped-call (pop)
#pragma wrapped-call (push, trampoline, 0x0b)
void showObjReleaseViewExhaustion(void);
#pragma wrapped-call (pop)
#pragma wrapped-call (push, trampoline, 0x09)
byte showObjDisplayVideo(void);
#pragma wrapped-call (pop)
#pragma wrapped-call (push, trampoline, 0x08)
void showObjWriteFreshView(byte valid);
#pragma wrapped-call (pop)
#pragma wrapped-call (push, trampoline, 7)
void showObjFreshViewRejectedCase(byte missing);
#pragma wrapped-call (pop)
#pragma wrapped-call (push, trampoline, 2)
void showObjFreshViewFailureStage(byte freeTiny);
#pragma wrapped-call (pop)
#pragma wrapped-call (push, trampoline, 0x0f)
void showObjFreshViewSuccessCase(void);
#pragma wrapped-call (pop)
#pragma wrapped-call (push, trampoline, 0x0f)
void showObjSmallSaveAvailableCase(void);
#pragma wrapped-call (pop)
#pragma wrapped-call (push, trampoline, 0x04)
unsigned long showObjMeasureTextRows(byte* saved, byte bank, byte firstRow, byte rows);
#pragma wrapped-call (pop)
#pragma wrapped-call (push, trampoline, 4)
void showObjPrintHex(unsigned long value);
#pragma wrapped-call (pop)
#pragma wrapped-call (push, trampoline, 0x09)
unsigned long showObjCycleCounter(void);
#pragma wrapped-call (pop)
#pragma wrapped-call (push, trampoline, 0x0f)
void showObjClippedDescriptionCase(View* view, char* originalDescription);
#pragma wrapped-call (pop)
extern byte b3PaletteRows;
extern byte showObjTestSavedRows, showObjTestSavedBank;
extern byte sendIrqCommand;
void showObjCheckSpriteOwnership(void);
void showObjCheckSelectedVramOwnership(byte freeThrough);
extern byte baselineAllocations[ALLOCATION_ARRAY_SIZE], currentAllocations[ALLOCATION_ARRAY_SIZE];
#define before baselineAllocations
#define after currentAllocations
static byte attrs[5];
byte showObjTestSavedRows, showObjTestSavedBank;
#define SHOWOBJ_BASELINE_CYCLES 0x00069037UL
void showObjCheckSpriteOwnership(void)
{
    unsigned i;
    byte savedBank = RAM_BANK;
    RAM_BANK = SPRITE_MEMORY_MANAGER_BANK;
    for (i=0; i<TOTAL_REAL_BLOCKS; ++i) {
        if (bDSpriteAllocTable[i]) {
            auditCheck(FALSE, "legacy show obj left a bitmap allocation behind");
            break;
        }
    }
    RAM_BANK = savedBank;
}
void showObjCheckSelectedVramOwnership(byte freeThrough)
{
    unsigned i;
    byte savedBank = RAM_BANK;
    RAM_BANK = SPRITE_MEMORY_MANAGER_BANK;
    for (i=0; i<TOTAL_REAL_BLOCKS; ++i) {
        if (bDSpriteAllocTable[i] != (i > freeThrough)) {
            auditCheck(FALSE, "selected legacy cel changed surrounding VRAM ownership");
            break;
        }
    }
    RAM_BANK = savedBank;
}
#pragma code-name (push, "BANKRAM0E")
static void showObjPrepareVramSlots(VeraSpriteAddress* selectedAddress, VeraSpriteAddress* neighbor)
{
    bDResetSpriteMemoryManager();
    /* Model all other live sprites without thousands of setup allocations. */
    memsetBanked(bDSpriteAllocTable, 1, TOTAL_REAL_BLOCKS, SPRITE_MEMORY_MANAGER_BANK);
    memsetBanked(bDSpriteAllocTable, 0, 2, SPRITE_MEMORY_MANAGER_BANK);
    bDResetSpriteTablePointer();
    *selectedAddress = bDFindFreeVramBlock(SPR_SIZE_8, SPR_SIZE_8);
    *neighbor = bDFindFreeVramBlock(SPR_SIZE_8, SPR_SIZE_8);
    auditCheck(*selectedAddress == VRAM_START && *neighbor == VRAM_START + 32,
        "selected legacy cel exhaustion fixture did not reserve adjacent 8x8 blocks");
    bDDeleteAllocation(*selectedAddress, SPR_SIZE_8, SPR_SIZE_8);
    bDResetSpriteTablePointer();
}
#pragma code-name (pop)

#pragma code-name (push, "BANKRAM12")
#pragma rodata-name (push, "BANKRAM12")
static void showObjSaveUnderApiCase(void)
{
    byte* savedText;
    byte savedBank, firstTextRow, savedTextRows;
    byte savedTextContext[14];
    byte previousActive = inventoryActive;
    View view;

    getLoadedView(&view, 200);
    showObjCheckTextContext(savedTextContext, TRUE);
    inventoryActive = TRUE;
    auditCheck(b3DisplayMessageBoxWithSaveUnder(view.description, view.codeBlockBank,
        14, AUTO_CALC_COLUMN, TEXTBOX_PALETTE_NUMBER, DEFAULT_BOX_WIDTH, TRUE,
        &savedText, &savedBank, &firstTextRow, &savedTextRows),
        "five-row save-under API did not open with one available block");
    auditCheck(savedText && firstTextRow == 17 && savedTextRows == 5,
        "five-row save-under API selected the wrong rows");
    memCpyBanked(&inventoryBuffer[0], savedText + 512, savedBank, 1);
    auditCheck(inventoryBuffer[0] == 0x8a,
        "five-row save-under API did not capture the last affected row");
    b3CopyTextRows(savedText, savedBank, firstTextRow, savedTextRows, TRUE);
    auditCheck(b10BankedDealloc(savedText, savedBank),
        "five-row save-under API leaked its allocation");
    memCpyBanked(&b3LastBoxLines, savedTextContext, TEXT_CODE_BANK, 2);
    memCpyBanked((byte*)&b3PaletteAddress, savedTextContext + 2, TEXT_CODE_BANK, 6);
    memCpyBanked((byte*)&displayTextAddressToCopyTo, savedTextContext + 8, TEXT_CODE_BANK, 4);
    memCpyBanked((byte*)&currentTextBuffer, savedTextContext + 12, TEXT_CODE_BANK, 2);
    showObjCheckTextContext(savedTextContext, FALSE);
    showObjCheckTextOverlay();
    inventoryActive = previousActive;
}
#pragma rodata-name (pop)
#pragma code-name (pop)
#pragma code-name (push, "BANKRAM09")
static byte pattern(unsigned address) { return (byte)(address ^ (address >> 8)); }
#pragma code-name (pop)
#pragma code-name (push, "BANKRAM09")
#pragma rodata-name (push, "BANKRAM09")
unsigned long showObjCycleCounter(void)
{
    byte a = *(volatile byte*)0x9fb8, b = *(volatile byte*)0x9fb9;
    byte c = *(volatile byte*)0x9fba, d = *(volatile byte*)0x9fbb;
    return ((unsigned long)d << 24) | ((unsigned long)c << 16) | ((unsigned)b << 8) | a;
}

#pragma rodata-name (pop)
#pragma code-name (pop)

#pragma code-name (push, "BANKRAM04")
#pragma rodata-name (push, "BANKRAM04")
void showObjPrintHex(unsigned long value)
{
    static const char digits[] = "0123456789ABCDEF";
    byte high, low;
    *(volatile byte*)0x9fbb = '0';
    *(volatile byte*)0x9fbb = '0';
    high = (byte)(value >> 20) & 15;
    low = (byte)(value >> 16) & 15;
    *(volatile byte*)0x9fbb = digits[high];
    *(volatile byte*)0x9fbb = digits[low];
    high = (byte)(value >> 12) & 15;
    low = (byte)(value >> 8) & 15;
    *(volatile byte*)0x9fbb = digits[high];
    *(volatile byte*)0x9fbb = digits[low];
    high = (byte)(value >> 4) & 15;
    low = (byte)value & 15;
    *(volatile byte*)0x9fbb = digits[high];
    *(volatile byte*)0x9fbb = digits[low];
}
#pragma rodata-name (pop)
#pragma code-name (pop)

#pragma code-name (push, "BANKRAM04")
#pragma rodata-name (push, "BANKRAM04")
unsigned long showObjMeasureTextRows(byte* saved, byte bank, byte firstRow, byte rows)
{
    unsigned long start, elapsed;
    word previousAddress;
    byte previousControl, previousAddressHigh;
    asm("php");
    asm("sei");
    previousControl = VERA.control;
    previousAddress = VERA.address;
    previousAddressHigh = VERA.address_hi;
    start = showObjCycleCounter();
    b3CopyTextRows(saved, bank, firstRow, rows, FALSE);
    b3CopyTextRows(saved, bank, firstRow, rows, TRUE);
    elapsed = showObjCycleCounter() - start;
    auditCheck(VERA.control == previousControl && VERA.address == previousAddress &&
        VERA.address_hi == previousAddressHigh,
        "showobj row transfer changed the caller VERA address");
    asm("plp");
    return elapsed;
}

#pragma rodata-name (pop)
#pragma code-name (pop)
#pragma code-name (push, "BANKRAM0F")
#pragma rodata-name (push, "BANKRAM0F")
void showObjSmallSaveAvailableCase(void)
{
    byte* block;
    byte bank, releaseBank;
    byte i, previousVideo, previousIrq, previousBank, previousActive;
    byte reservedAttributes[16], roomPixels[3];
    unsigned long baselineCycles, targetCycles;
    View view;
    char* originalDescription;

    block = b10TryBankedAlloc(640, &bank);
    auditCheck(block != 0, "showobj target alloc");
    auditOutput("SHOWOBJ BASE 8192 B / cycles 0x");
    baselineCycles = SHOWOBJ_BASELINE_CYCLES;
    showObjPrintHex(baselineCycles);
    auditOutput("; ");
    auditOutput("; ROWS 1280 B / cycles 0x");
    previousBank = RAM_BANK;
    asm("php");
    asm("sei");
    targetCycles = showObjMeasureTextRows(block, bank, 17, 5);
    auditCheck(testStatus() & 4, "showobj row transfer re-enabled caller IRQs");
    auditCheck(RAM_BANK == previousBank, "showobj row transfer changed the caller RAM bank");
    auditCheck(inventoryActive && !sendIrqCommand,
        "showobj row transfer changed modal state or left a pending IRQ command");
    auditCheck(testPeek(MAPBASE + 17UL * TILE_LAYER_BYTES_PER_ROW) == 0x88,
        "showobj row transfer failed to restore its first map byte");
    auditCheck(testPeek(MAPBASE + 17UL * TILE_LAYER_BYTES_PER_ROW + 1) == 0x89,
        "showobj row transfer failed to restore its first palette byte");
    showObjCheckTextRow();
    showObjPrintHex(targetCycles);
    showObjCheckTextOverlay();
    auditOutput("; ");
    memCpyBanked(inventoryBuffer, block, bank, 1);
    auditCheck(inventoryBuffer[0] == 0x88, "showobj first saved row did not capture its map bytes");
    memCpyBanked(inventoryBuffer, block + 512, bank, 1);
    auditCheck(inventoryBuffer[0] == 0x8a, "showobj fifth saved row did not capture its map bytes");
    asm("plp");
    auditCheck(b10BankedDealloc(block, bank) && targetCycles < baselineCycles / 2,
        "showobj target");

    memCpyBanked(before, allocationArray, MEMORY_MANAGEMENT_BANK, sizeof(before));
    block = b10TryBankedAlloc(640, &releaseBank);
    auditCheck(block != 0, "showobj fixture");
    while (b10TryBankedAlloc(640, &bank)) {}
    auditCheck(!b10TryBankedAlloc(640, &bank),
        "showobj fixture");
    auditCheck(b10BankedDealloc(block, releaseBank),
        "showobj fixture");
    memCpyBanked(after, allocationArray, MEMORY_MANAGEMENT_BANK, sizeof(after));

    showObjSaveUnderApiCase();

    testVisibleFrames = 0;
    showObjTestSavedRows = showObjTestSavedBank = 0xff;
    previousActive = inventoryActive;
    inventoryActive = TRUE;
    testPutKey('x');
    b11ShowObj(200);
    bDResetSpriteTablePointer();
    auditCheck(showObjTestSavedRows == 5 && showObjTestSavedBank == 17,
        "actual inspect did not restore its five affected text rows");
    memCpyBanked(&inventoryBuffer[0], block + 512, releaseBank, 1);
    auditCheck(inventoryBuffer[0] == 0x8a,
        "actual inspect did not retain the fifth row in its RAM save-under");
    auditCheck(inventoryActive, "legacy show obj changed its caller's modal state");
    showObjCheckSelectedVramOwnership(0);

    auditCheck(testVisibleFrames && !cbm_k_getin(),
        "legacy show obj still requires a 4096-byte save-under when 640 bytes are free");
    showObjCheckTextOverlay();
    inventoryActive = previousActive;
    showObjReleaseViewExhaustion();
    memCpyBanked(after, allocationArray, MEMORY_MANAGEMENT_BANK, sizeof(after));
    auditCheck(!memcmp(before, after, sizeof(after)),
        "showobj save leak");

    getLoadedView(&view, 200);
    originalDescription = view.description;
    previousVideo = showObjDisplayVideo();
    previousIrq = testStatus() & 4;
    previousBank = RAM_BANK;
    previousActive = inventoryActive;
    for (i = 0; i < sizeof(reservedAttributes); ++i)
        reservedAttributes[i] = testPeek(0x1fff0UL + i);
    roomPixels[0] = testPeek(0);
    roomPixels[1] = testPeek(0x4800UL);
    roomPixels[2] = testPeek(0x95ffUL);
    memCpyBanked(before, allocationArray, MEMORY_MANAGEMENT_BANK, sizeof(before));
    view.description = 0;
    setLoadedView(&view, 200);
    while (b10TryBankedAlloc(640, &bank)) {}
    auditCheck(!b10TryBankedAlloc(640, &bank),
        "showobj fixture");
    memCpyBanked(after, allocationArray, MEMORY_MANAGEMENT_BANK, sizeof(after));
    testVisibleFrames = 0;
    testPutKey('n');
    b11ShowObj(200);
    bDResetSpriteTablePointer();
    showObjCheckSelectedVramOwnership(0);
    auditCheck(testVisibleFrames && !cbm_k_getin(),
        "showobj null description allocation");
    for (i = 0; i < sizeof(reservedAttributes); ++i) {
        auditCheck(testPeek(0x1fff0UL + i) == reservedAttributes[i],
            "showobj mouse attrs");
    }
    auditCheck(inventoryActive == previousActive &&
        (showObjDisplayVideo() & 0x7f) == (previousVideo & 0x7f) &&
        (testStatus() & 4) == previousIrq && RAM_BANK == previousBank,
        "showobj caller state");
    auditCheck(testPeek(0) == roomPixels[0] &&
        testPeek(0x4800UL) == roomPixels[1] &&
        testPeek(0x95ffUL) == roomPixels[2],
        "showobj room pixels");
    showObjCheckTextOverlay();
    memCpyBanked(GOLDEN_RAM_WORK_AREA + 32, allocationArray,
        MEMORY_MANAGEMENT_BANK, sizeof(after));
    auditCheck(!memcmp(after, GOLDEN_RAM_WORK_AREA + 32, sizeof(after)),
        "showobj unused allocation");
    showObjReleaseViewExhaustion();
    memCpyBanked(after, allocationArray, MEMORY_MANAGEMENT_BANK, sizeof(after));
    auditCheck(!memcmp(before, after, sizeof(before)),
        "showobj fixture leak");
    view.description = originalDescription;
    setLoadedView(&view, 200);

    for (i = 0; i < 64; ++i) inventoryBuffer[i] = i % 9 == 8 ? ' ' : 'A';
    inventoryBuffer[64] = 0;
    memCpyBanked(view.codeBlock + 1000, inventoryBuffer, view.codeBlockBank, 65);
    view.description = (char*)view.codeBlock + 1000;
    setLoadedView(&view, 200);
    testVisibleFrames = 0;
    showObjCheckTextContext(after, 1);
    testPutKey('w');
    b11ShowObj(200);
    bDResetSpriteTablePointer();
    showObjCheckSelectedVramOwnership(0);
    showObjCheckTextContext(after, 0);
    auditCheck(testVisibleFrames && !cbm_k_getin(), "showobj wrapped description");
    showObjCheckTextOverlay();

    showObjClippedDescriptionCase(&view, originalDescription);
}
#pragma rodata-name (pop)
#pragma code-name (pop)

#pragma code-name (push, "BANKRAM0F")
#pragma rodata-name (push, "BANKRAM0F")
void showObjClippedDescriptionCase(View* view, char* originalDescription)
{
    byte i, previousVideo, previousIrq, previousBank, previousActive;
    byte reservedAttributes[16];
    byte terminator = 0, length, j;
    unsigned offset;

    for (offset = 0; offset < 512; offset += 80) {
        length = offset + 80 > 512 ? (byte)(512 - offset) : 80;
        for (j = 0; j < length; ++j)
            inventoryBuffer[j] = (offset + j) % 8 == 7 ? ' ' : 'A';
        memCpyBanked(view->codeBlock + 1000 + offset, inventoryBuffer,
            view->codeBlockBank, length);
    }
    memCpyBanked(view->codeBlock + 1512, &terminator, view->codeBlockBank, 1);
    view->description = (char*)view->codeBlock + 1000;
    setLoadedView(view, 200);
    for (i = 0; i < sizeof(reservedAttributes); ++i)
        reservedAttributes[i] = testPeek(0x1fff0UL + i);
    previousVideo = showObjDisplayVideo();
    previousIrq = testStatus() & 4;
    previousBank = RAM_BANK;
    previousActive = inventoryActive;
    memCpyBanked(before, allocationArray, MEMORY_MANAGEMENT_BANK, sizeof(before));
    memCpyBanked(attrs, &b3LastBoxLines, TEXT_CODE_BANK, 1);
    memCpyBanked(attrs + 1, &b3LastBoxStartLine, TEXT_CODE_BANK, 1);
    memCpyBanked(attrs + 2, &b3PaletteRows, TEXT_CODE_BANK, 1);
    showObjCheckSelectedVramOwnership(0);
    testVisibleFrames = 0;
    testPutKey('c');
    b11ShowObj(200);
    bDResetSpriteTablePointer();
    memCpyBanked(attrs + 3, &b3LastBoxLines, TEXT_CODE_BANK, 1);
    memCpyBanked(attrs + 4, &b3LastBoxStartLine, TEXT_CODE_BANK, 1);
    memCpyBanked(inventoryBuffer, &b3PaletteRows, TEXT_CODE_BANK, 1);
    auditCheck(!testVisibleFrames && cbm_k_getin() == 'c' && attrs[0] == attrs[3] &&
        attrs[1] == attrs[4] && attrs[2] == inventoryBuffer[0],
        "clipped description changed text state or consumed input");
    for (i = 0; i < sizeof(reservedAttributes); ++i)
        auditCheck(testPeek(0x1fff0UL + i) == reservedAttributes[i],
            "clipped description changed mouse or inspect attributes");
    auditCheck(inventoryActive == previousActive &&
        (showObjDisplayVideo() & 0x7f) == (previousVideo & 0x7f) &&
        (testStatus() & 4) == previousIrq && RAM_BANK == previousBank,
        "clipped description changed caller state");
    showObjCheckTextOverlay();
    memCpyBanked(after, allocationArray, MEMORY_MANAGEMENT_BANK, sizeof(after));
    auditCheck(!memcmp(before, after, sizeof(before)), "clipped description leaked allocations");
    showObjCheckSelectedVramOwnership(0);
    view->description = originalDescription;
    setLoadedView(view, 200);
}
#pragma rodata-name (pop)
#pragma code-name (pop)

#pragma code-name (push, "BANKRAM09")
#pragma rodata-name (push, "BANKRAM09")
void showObjCheckTextOverlay(void)
{
	unsigned offset;
	byte i;
    asm("php");
    asm("sei");
	auditCheck(testPeek(MAPBASE + 3200UL) == pattern(3200),
		"text overlay changed row 25 before map scan");
	for (offset = 0; offset < 4096; offset += 64) {
        /* The live game IRQ owns its status row while this map is sampled. */
        if (offset / TILE_LAYER_BYTES_PER_ROW == 7) continue;
		inventoryAddress = MAPBASE + (unsigned long)offset;
		inventoryLength = 64;
		inventoryWrite = FALSE;
		inventoryTransfer();
		for (i = 0; i < 64; ++i) {
			if (inventoryBuffer[i] == pattern(offset + i)) continue;
			auditCheck(FALSE, "legacy show obj did not restore full text overlay");
			goto done;
        }
    }
	done:
    asm("plp");
}
void showObjCheckTextRow(void)
{
    auditCheck(testPeek(MAPBASE + 3200UL) == 140,
        "successful inspect changed an unrelated text row");
    inventoryAddress = MAPBASE + 3200UL;
    inventoryLength = 64;
    inventoryWrite = FALSE;
    inventoryTransfer();
    auditCheck(inventoryBuffer[0] == 140,
        "single row transfer did not read the preserved text row");
}
void showObjSeedTextOverlay(void)
{
    unsigned offset;
    byte i, length;
    for (offset=0; offset<4096; offset+=80) {
        length = offset + 80 > 4096 ? (byte)(4096 - offset) : 80;
        for (i=0; i<length; ++i) inventoryBuffer[i] = pattern(offset+i);
        inventoryAddress = MAPBASE+(unsigned long)offset;
        inventoryLength = length;
        inventoryWrite = TRUE;
        inventoryTransfer();
    }
}
static void showObjTallAuditStages(View* view, Loop* loop, Cel* cel)
{
    showObjTallCelCase(view, loop, cel, 64);
    testDisableInterrupts();
    auditCheck(!sendIrqCommand, "valid tall show obj returned with a pending text IRQ");
    showObjCheckTextRow();
    showObjCheckTextOverlay();
    testEnableInterrupts();
    showObjTallCelCase(view, loop, cel, 65);
}
void showObjCheckTextContext(byte* saved, byte save)
{
    if (save) {
        memCpyBanked(saved, &b3LastBoxLines, TEXT_CODE_BANK, 2);
        memCpyBanked(saved + 2, (byte*)&b3PaletteAddress, TEXT_CODE_BANK, 6);
        memCpyBanked(saved + 8, (byte*)&displayTextAddressToCopyTo, TEXT_CODE_BANK, 4);
        memCpyBanked(saved + 12, (byte*)&currentTextBuffer, TEXT_CODE_BANK, 2);
    } else {
        memCpyBanked(inventoryBuffer, &b3LastBoxLines, TEXT_CODE_BANK, 2);
        memCpyBanked(inventoryBuffer + 2, (byte*)&b3PaletteAddress, TEXT_CODE_BANK, 6);
        memCpyBanked(inventoryBuffer + 8, (byte*)&displayTextAddressToCopyTo, TEXT_CODE_BANK, 4);
        memCpyBanked(inventoryBuffer + 12, (byte*)&currentTextBuffer, TEXT_CODE_BANK, 2);
        auditCheck(!memcmp(saved, inventoryBuffer, 14), "legacy show obj changed text context");
    }
}
#pragma rodata-name (pop)
#pragma code-name (pop)
#pragma code-name (push, "BANKRAM07")
#pragma rodata-name (push, "BANKRAM07")
void showObjExhaustionCases(void)
{
    byte bank, i, previousVideo, previousIrq, previousBank, previousActive;
    byte reservedAttributes[16], roomPixels[3];
    View view;
    memCpyBanked(before, allocationArray, MEMORY_MANAGEMENT_BANK, sizeof(before));
    previousVideo = showObjDisplayVideo();
    previousIrq = testStatus() & 4;
    previousBank = RAM_BANK;
    previousActive = inventoryActive;
    for (i = 0; i < sizeof(reservedAttributes); ++i)
        reservedAttributes[i] = testPeek(0x1fff0UL + i);
    roomPixels[0] = testPeek(0);
    roomPixels[1] = testPeek(0x4800UL);
    roomPixels[2] = testPeek(0x95ffUL);
    while (b10TryBankedAlloc(640, &bank)) {}
    auditCheck(!b10TryBankedAlloc(640, &bank),
        "legacy RAM exhaustion fixture left a renderer-sized save-under block free");
    memCpyBanked(after, allocationArray, MEMORY_MANAGEMENT_BANK, sizeof(after));
    testVisibleFrames = 0;
    testPutKey('e');
    b11ShowObj(200);
    getLoadedView(&view, 200);
    auditCheck(view.loaded, "legacy save-under allocation failure discarded a loaded view");
    auditCheck(!testVisibleFrames && cbm_k_getin() == 'e',
        "legacy save-under allocation failure published or consumed a partial overlay");
    auditCheck(inventoryActive == previousActive &&
        (showObjDisplayVideo() & 0x7f) == (previousVideo & 0x7f) &&
        (testStatus() & 4) == previousIrq && RAM_BANK == previousBank,
        "legacy save-under allocation failure changed caller display, IRQ or bank state");
    for (i = 0; i < sizeof(reservedAttributes); ++i)
        auditCheck(testPeek(0x1fff0UL + i) == reservedAttributes[i],
            "legacy save-under allocation failure changed mouse or inspect attributes");
    auditCheck(testPeek(0) == roomPixels[0] && testPeek(0x4800UL) == roomPixels[1] &&
        testPeek(0x95ffUL) == roomPixels[2],
        "legacy save-under allocation failure changed room pixels");
    showObjCheckTextOverlay();
    memCpyBanked(GOLDEN_RAM_WORK_AREA, allocationArray, MEMORY_MANAGEMENT_BANK, sizeof(after));
    auditCheck(!memcmp(after, GOLDEN_RAM_WORK_AREA, sizeof(after)),
        "legacy save-under allocation failure leaked its exhausted fixture state");
    showObjReleaseViewExhaustion();
    memCpyBanked(after, allocationArray, MEMORY_MANAGEMENT_BANK, sizeof(after));
    auditCheck(!memcmp(before, after, sizeof(before)),
        "legacy save-under allocation failure leaked RAM allocations");

    while (bDFindFreeVramBlock(SPR_SIZE_8, SPR_SIZE_8)) {}
    b11ShowObj(200);
    getLoadedView(&view, 200);
    auditCheck(view.loaded, "legacy VRAM exhaustion discarded a loaded view");
    bDResetSpriteMemoryManager();
}
#pragma rodata-name (pop)
#pragma code-name (pop)
#pragma code-name (push, "BANKRAM0C")
#pragma rodata-name (push, "BANKRAM0C")
static void showObjWideCelCase(View* view, Loop* loop, Cel* cel, VeraSpriteAddress neighbor)
{
    byte previousVideo, currentVideo;
    View returned;
    cel->width = 128;
    cel->height = 8;
    setLoadedCel(loop, cel, 0);
    setLoadedLoop(view, loop, 0);
    setLoadedView(view, 200);
    inventoryActive = TRUE;
    testDisableInterrupts();
    previousVideo = VERA.display.video;
    testEnableInterrupts();
    testPutKey('w');
    b11ShowObj(200);
    testDisableInterrupts();
    currentVideo = VERA.display.video;
    testEnableInterrupts();
    getLoadedView(&returned, 200);
        auditCheck((currentVideo & 0x7f) == (previousVideo & 0x7f) && inventoryActive && cbm_k_getin() == 'w',
        "oversized doubled legacy cel changed caller state");
    auditCheck(returned.loaded && returned.codeBlock == view->codeBlock,
        "oversized doubled legacy cel discarded its caller-owned view");
    auditCheck(testPeek(neighbor) == 0xA5, "oversized doubled legacy cel changed neighboring VRAM");
}
#pragma rodata-name (pop)
#pragma code-name (pop)
#pragma code-name (push, "BANKRAM09")
#pragma rodata-name (push, "BANKRAM09")
byte showObjDisplayVideo(void)
{
    byte control, video;
    asm("php");
    asm("sei");
    control = VERA.control;
    VERA.control = control & 1; /* DCSEL 0 exposes DC_VIDEO at $9F29. */
    video = VERA.display.video;
    VERA.control = control;
    asm("plp");
    return video;
}
#pragma rodata-name (pop)
#pragma code-name (pop)

#pragma code-name (push, "BANKRAM07")
#pragma rodata-name (push, "BANKRAM07")
void showObjFreshViewRejectedCase(byte missing)
{
    byte previousActive = inventoryActive, previousIrq = testStatus() & 4;
    byte previousBank = RAM_BANK, previousVideo = showObjDisplayVideo(), currentVideo;
    byte previousAttributes[8], currentAttributes[8], i;
    AGIFilePosType position;
    View view;
    memCpyBanked(before, allocationArray, MEMORY_MANAGEMENT_BANK, sizeof(before));
    showObjCheckSpriteOwnership();
    for (i = 0; i < sizeof(previousAttributes); ++i) previousAttributes[i] = testPeek(0x1fff8UL + i);
    if (missing) {
        memset(&position, 0, sizeof(position));
        position.fileNum = 8;
        position.filePos = EMPTY;
        memCpyBanked((byte*)&viewdir[200], (byte*)&position, DIRECTORY_BANK, sizeof(position));
    } else {
        showObjWriteFreshView(FALSE);
        memset(&position, 0, sizeof(position));
        position.fileNum = 8;
        memCpyBanked((byte*)&viewdir[200], (byte*)&position, DIRECTORY_BANK, sizeof(position));
    }
    inventoryActive = TRUE;
    testPutKey('q');
    b11ShowObj(200);
    getLoadedView(&view, 200);
    currentVideo = showObjDisplayVideo();
    for (i = 0; i < sizeof(currentAttributes); ++i) currentAttributes[i] = testPeek(0x1fff8UL + i);
    auditCheck(!view.loaded, "bad VIEW failure");
    auditCheck(inventoryActive && (currentVideo & 0x7f) == (previousVideo & 0x7f) &&
        !memcmp(previousAttributes, currentAttributes, sizeof(previousAttributes)),
        "bad VIEW failure");
    auditCheck((testStatus() & 4) == previousIrq && RAM_BANK == previousBank,
        "bad VIEW failure");
    auditCheck(cbm_k_getin() == 'q', "bad VIEW failure");
    memCpyBanked(after, allocationArray, MEMORY_MANAGEMENT_BANK, sizeof(after));
    auditCheck(!memcmp(before, after, sizeof(before)), "bad VIEW failure");
    showObjCheckSpriteOwnership();
    inventoryActive = previousActive;
}

#pragma code-name (pop)
#pragma rodata-name (pop)
#pragma code-name (push, "BANKRAM01")
#pragma rodata-name (push, "BANKRAM01")
void showObjPrepareViewExhaustion(byte freeTiny)
{
    static const unsigned sizes[] = {LARGE_SIZE, MEDIUM_SIZE, SMALL_SIZE, EXTRA_SMALL_SIZE};
    byte i, bank;
    byte* block;
    for (i = 0; i < sizeof(sizes) / sizeof(sizes[0]); ++i) {
        while (TRUE) {
            block = b10TryBankedAlloc(sizes[i], &bank);
            if (!block) break;
        }
    }
    for (i = 0; i < TINY_NO_SEGMENTS - freeTiny; ++i) {
        block = b10TryBankedAlloc(TINY_SIZE, &bank);
        auditCheck(block != 0, "VIEW capacity");
    }
    if (!freeTiny)
        auditCheck(!b10TryBankedAlloc(TINY_SIZE, &bank), "VIEW exhaustion");
    memCpyBanked(after, allocationArray, MEMORY_MANAGEMENT_BANK, sizeof(after));
}

#pragma rodata-name (pop)
#pragma code-name (pop)
#pragma code-name (push, "BANKRAM0B")
#pragma rodata-name (push, "BANKRAM0B")
void showObjReleaseViewExhaustion(void)
{
    static const unsigned sizes[] = {TINY_SIZE, EXTRA_SMALL_SIZE, SMALL_SIZE,
        MEDIUM_SIZE, LARGE_SIZE};
    static const unsigned counts[] = {TINY_NO_SEGMENTS, EXTRA_SMALL_NO_SEGMENTS,
        SMALL_NO_SEGMENTS, MEDIUM_NO_SEGMENTS, LARGE_NO_SEGMENTS};
    static const byte firstBanks[] = {TINY_FIRST_BANK, EXTRA_SMALL_FIRST_BANK,
        SMALL_FIRST_BANK, MEDIUM_FIRST_BANK, LARGE_FIRST_BANK};
    unsigned long offset;
    unsigned int index = 0, segment;
    byte area;

    for (area = 0; area < NO_SIZES; ++area) {
        for (segment = 0; segment < counts[area]; ++segment, ++index) {
            if (!before[index] && after[index]) {
                offset = (unsigned long)segment * sizes[area];
                auditCheck(b10BankedDealloc((byte*)(BANK_MIN + offset % MEMORY_MANAGER_BANK_SIZE),
                    firstBanks[area] + offset / MEMORY_MANAGER_BANK_SIZE),
                    "showobj pressure fixture release failed");
            }
        }
    }
}
#pragma rodata-name (pop)
#pragma code-name (pop)
#pragma code-name (push, "BANKRAM02")
#pragma rodata-name (push, "BANKRAM02")
void showObjFreshViewFailureStage(byte freeTiny)
{
    byte i, receivedKey, row25, savedAttributes[8];
    byte previousActive = inventoryActive, previousVideo, currentVideo;
    byte previousIrq = testStatus() & 4, previousBank = RAM_BANK;
    byte previousControl = VERA.control;
    byte* scratch = (byte*)GOLDEN_RAM_WORK_AREA;
    View* view = (View*)(scratch + 64);

    row25 = testPeek(MAPBASE + 3200UL);
    memCpyBanked(before, allocationArray, MEMORY_MANAGEMENT_BANK, sizeof(before));
    showObjCheckSpriteOwnership();
    if (freeTiny == 4) {
        auditCheck(b9TryLoadViewFile(200), "fresh VIEW save-under load preflight");
        showObjCheckTextContext(scratch + 16, TRUE);
    }
    previousVideo = showObjDisplayVideo();
    for (i = 0; i < sizeof(savedAttributes); ++i)
        savedAttributes[i] = testPeek(0x1fff8UL + i);
    if (freeTiny == 4) {
        attrs[0] = testPeek(0);
        attrs[1] = testPeek(0x4800UL);
        attrs[2] = testPeek(0x95ffUL);
    }
    inventoryActive = TRUE;
    testPutKey('q');
    showObjPrepareViewExhaustion(freeTiny);
    auditCheck(testPeek(MAPBASE + 3200UL) == row25,
        "VIEW allocation exhaustion changed map row 25");
    if (freeTiny == 4) {
        testVisibleFrames = 0;
    }
    b11ShowObj(200);
    auditCheck(testPeek(MAPBASE + 3200UL) == row25,
        "fresh VIEW inspection changed map row 25 before cleanup");
    getLoadedView(view, 200);
    currentVideo = showObjDisplayVideo();
    auditCheck(view->loaded == (freeTiny == 4), "fresh VIEW failure changed view ownership");
    auditCheck(VERA.control == previousControl, "fresh VIEW failure changed VERA control");
    auditCheck(inventoryActive && (currentVideo & 0x7f) == (previousVideo & 0x7f),
        "fresh VIEW failure changed modal or video state");
    for (i = 0; i < sizeof(savedAttributes); ++i)
        auditCheck(savedAttributes[i] == testPeek(0x1fff8UL + i),
            "fresh VIEW failure changed sprite attributes");
    auditCheck((testStatus() & 4) == previousIrq, "fresh VIEW failure changed the caller IRQ mask");
    auditCheck(RAM_BANK == previousBank, "fresh VIEW failure changed the caller RAM bank");
    receivedKey = cbm_k_getin();
    if (receivedKey != 'q') {
        auditCheck(FALSE, freeTiny == 4 ? "fresh VIEW save-under failure consumed the caller key" :
            "fresh VIEW loading failure consumed the caller key");
    }
    if (freeTiny == 4) {
        auditCheck(!testVisibleFrames, "fresh VIEW save-under failure displayed a partial overlay");
        auditCheck(testPeek(0) == attrs[0] && testPeek(0x4800UL) == attrs[1] &&
            testPeek(0x95ffUL) == attrs[2], "fresh VIEW save-under failure changed room pixels");
        showObjCheckTextContext(scratch + 16, FALSE);
    }
    memCpyBanked(GOLDEN_RAM_WORK_AREA, allocationArray, MEMORY_MANAGEMENT_BANK, sizeof(after));
    auditCheck(!memcmp(after, GOLDEN_RAM_WORK_AREA, sizeof(after)),
        "fresh VIEW failure changed the exhausted allocator snapshot");
    if (freeTiny == 4) {
        b9DiscardView(200);
        memCpyBanked(after, allocationArray, MEMORY_MANAGEMENT_BANK, sizeof(after));
    }
    showObjReleaseViewExhaustion();
    memCpyBanked(after, allocationArray, MEMORY_MANAGEMENT_BANK, sizeof(after));
    auditCheck(!memcmp(before, after, sizeof(before)),
        "fresh VIEW failure left the allocator different after fixture cleanup");
    showObjCheckSpriteOwnership();
    inventoryActive = previousActive;
}

#pragma code-name (pop)
#pragma rodata-name (pop)
#pragma code-name (push, "BANKRAM0F")
#pragma rodata-name (push, "BANKRAM0F")
void showObjFreshViewSuccessCase(void)
{
    byte previousActive = inventoryActive, previousIrq = testStatus() & 4;
    byte previousBank = RAM_BANK, previousVideo = showObjDisplayVideo(), currentVideo;
    byte previousAttributes[8], currentAttributes[8], i;
    View view;
    memCpyBanked(before, allocationArray, MEMORY_MANAGEMENT_BANK, sizeof(before));
    showObjCheckSpriteOwnership();
    for (i = 0; i < sizeof(previousAttributes); ++i) previousAttributes[i] = testPeek(0x1fff8UL + i);
    inventoryActive = TRUE;
    testVisibleFrames = 0;
    testPutKey('x');
    b11ShowObj(200);
    currentVideo = showObjDisplayVideo();
    for (i = 0; i < sizeof(currentAttributes); ++i) currentAttributes[i] = testPeek(0x1fff8UL + i);
    getLoadedView(&view, 200);
    auditCheck(!view.loaded, "fresh VIEW result");
    auditCheck(testPeek(VRAM_START) == 0x77 && testVisibleFrames && !cbm_k_getin(),
        "fresh VIEW result");
    auditCheck(inventoryActive && (currentVideo & 0x7f) == (previousVideo & 0x7f) &&
        !memcmp(previousAttributes, currentAttributes, sizeof(previousAttributes)),
        "fresh VIEW result");
    auditCheck((testStatus() & 4) == previousIrq && RAM_BANK == previousBank,
        "fresh VIEW result");
    memCpyBanked(after, allocationArray, MEMORY_MANAGEMENT_BANK, sizeof(after));
    auditCheck(!memcmp(before, after, sizeof(before)), "fresh VIEW result");
    showObjCheckSpriteOwnership();
    inventoryActive = previousActive;
}

#pragma code-name (pop)
#pragma rodata-name (pop)
#pragma code-name (push, "BANKRAM08")
#pragma rodata-name (push, "BANKRAM08")
void showObjWriteFreshView(byte valid)
{
    static const byte resource[] = {
        0x12, 0x34, 0, 41, 0,
        27, 0, 2, 27, 0, 9, 0, 18, 0,
        1, 3, 0, 16, 1, 0, 0x78, 0x08, 0,
        1, 3, 0, 16, 1, 0, 0x78, 0x08, 0,
        'A', ' ', 'D', 'E', 'S', 'C', 'R', 'I', 'P', 'T', 'I', 'O', 'N', 0
    };
    static const byte invalid[] = {0, 0, 0, 0, 0};
    const byte* bytes = valid ? resource : invalid;
    unsigned length = valid ? sizeof(resource) : sizeof(invalid);
    FILE* output = fopen("vol.8", "wb");
    auditCheck(output != 0, "VIEW fixture");
    auditCheck(fwrite(bytes, 1, length, output) == length, "VIEW fixture");
    fclose(output);
}
#pragma code-name (pop)
#pragma rodata-name (pop)

void showObjFreshViewCases(void)
{
    byte i;
    AGIFilePosType previousPosition, position;
    memCpyBanked((byte*)&previousPosition, (byte*)&viewdir[200], DIRECTORY_BANK, sizeof(previousPosition));
    showObjWriteFreshView(TRUE);
    memset(&position, 0, sizeof(position));
    position.fileNum = 8;
    memCpyBanked((byte*)&viewdir[200], (byte*)&position, DIRECTORY_BANK, sizeof(position));
    for (i = 0; i < 4; ++i) {
        showObjFreshViewFailureStage(i);
    }
    showObjFreshViewFailureStage(4);
    showObjFreshViewRejectedCase(TRUE);
    showObjFreshViewRejectedCase(FALSE);
    showObjWriteFreshView(TRUE);
    showObjFreshViewSuccessCase();
    memCpyBanked((byte*)&viewdir[200], (byte*)&previousPosition, DIRECTORY_BANK, sizeof(previousPosition));
    auditCheck(remove("vol.8") == 0, "VIEW fixture");
}
#pragma code-name (push, "BANKRAM0F")
void showObjTallCelCase(View* view, Loop* loop, Cel* cel, byte height)
{
    byte row, rle[3] = {0x78, 0x08, 0};
    byte previousVideo, currentVideo, liveAllocation;
    View returned;
    VeraSpriteAddress image, neighbor;
    for (row = 0; row < height; ++row)
        memCpyBanked(view->codeBlock + (unsigned)row * sizeof(rle), rle, view->codeBlockBank, sizeof(rle));
    cel->bmp = view->codeBlock;
    cel->bitmapBank = view->codeBlockBank;
    cel->width = 16;
    cel->height = height;
    loop->allocationWidth = SPR_ATTR_32;
    loop->allocationHeight = SPR_ATTR_64;
    setLoadedCel(loop, cel, 0);
    setLoadedLoop(view, loop, 0);
    setLoadedView(view, 200);

    bDResetSpriteMemoryManager();
    image = bDFindFreeVramBlock(SPR_SIZE_32, SPR_SIZE_64);
    neighbor = bDFindFreeVramBlock(SPR_SIZE_32, SPR_SIZE_64);
    auditCheck(image == VRAM_START && neighbor == VRAM_START + 1024,
        "legacy show obj adjacent-span fixture did not allocate contiguous spans");
    bDDeleteAllocation(image, SPR_SIZE_32, SPR_SIZE_64);
    bDResetSpriteTablePointer();
    testPoke(0xa5, neighbor);
    if (height != 64) {
        testDisableInterrupts();
        previousVideo = VERA.display.video;
        testEnableInterrupts();
        showObjCheckTextRow();
    }
    inventoryActive = TRUE;
    testVisibleFrames = 0;
    testPutKey('x');
    b11ShowObj(200);
    if (height == 64) {
        auditCheck(testPeek(image) == 0x77 && testPeek(image + 63UL * 16) == 0x77,
            "valid 16x64 legacy cel did not render through its final row");
        auditCheck(testPeek(neighbor) == 0xa5, "valid 16x64 legacy cel overwrote the neighboring VRAM allocation");
        auditCheck(!cbm_k_getin(), "valid 16x64 legacy show obj did not consume its close event");
    } else {
        auditCheck(testPeek(neighbor) == 0xa5, "16x65 legacy show obj overwrote the neighboring VRAM allocation");
        testDisableInterrupts();
        currentVideo = VERA.display.video;
        testEnableInterrupts();
        auditCheck((currentVideo & 0x7f) == (previousVideo & 0x7f),
            "unsupported tall legacy cel changed display flags");
        auditCheck(inventoryActive, "unsupported tall legacy cel changed the caller's modal state");
        auditCheck(cbm_k_getin() == 'x', "unsupported tall legacy cel consumed the caller's close event");
        getLoadedView(&returned, 200);
        auditCheck(returned.loaded && returned.codeBlock == view->codeBlock,
            "unsupported tall legacy cel discarded the caller-owned view");
    }
    memCpyBanked(&liveAllocation, bDSpriteAllocTable+32, SPRITE_MEMORY_MANAGER_BANK, 1);
    auditCheck(liveAllocation != 0, "legacy show obj released the live neighboring VRAM allocation");
    bDDeleteAllocation(neighbor, SPR_SIZE_32, SPR_SIZE_64);
}
#pragma code-name (pop)
void showObjAuditCases(void)
{
    VeraSpriteAddress selectedAddress, neighbor;
    showObjPrepareVramSlots(&selectedAddress, &neighbor);
    showObjAuditCasesInBank(selectedAddress, neighbor);
}
#pragma code-name (push, "BANKRAM01")
void showObjAuditCasesInBank(VeraSpriteAddress selectedAddress, VeraSpriteAddress neighbor)
{
    View view, returned;
    Loop loop;
    Cel cel, unselected;
    byte i, previousVideo, returnedVideo, previousControl, returnedControl;
    byte previousIrq, returnedIrq, previousBank, returnedBank;
    byte* bytes = (byte*)GOLDEN_RAM_WORK_AREA;
    byte reservedAttributes[16], roomPixels[3];
    memset(&view, 0, sizeof(view));
    memset(&loop, 0, sizeof(loop));
    memset(&cel, 0, sizeof(cel));
    view.loaded = TRUE;
    view.numberOfLoops = 1;
    view.maxCels = 2;
    view.maxVeraSlots = 1;
    view.loops = (Loop*)b10BankedAlloc(sizeof(loop), &view.loopsBank);
    loop.numberOfCels = 2;
    loop.cels = (Cel*)b10BankedAlloc(sizeof(cel)*2, &loop.celsBank);
    loop.allocationWidth = SPR_ATTR_64;
    loop.allocationHeight = SPR_ATTR_64;
    /* Keep the real source bitmap in a different bank from its cel metadata. */
    view.codeBlock = b10BankedAlloc(1601, &view.codeBlockBank);
    auditCheck(view.codeBlockBank != loop.celsBank, "legacy cel fixture did not exercise a separate bitmap bank");
    for (i=0; i<8; ++i) { bytes[i*2] = 0x74; bytes[i*2+1] = 0; }
    memcpy(bytes+16, "an object", 10);
    bytes[27] = 0xA5;
    memCpyBanked(view.codeBlock, bytes, view.codeBlockBank, 16);
    memCpyBanked(view.codeBlock+256, bytes+16, view.codeBlockBank, 10);
    memCpyBanked(view.codeBlock+512, bytes+27, view.codeBlockBank, 1);
    view.description = (char*)view.codeBlock+256;
    cel.bmp = view.codeBlock;
    cel.bitmapBank = view.codeBlockBank;
    cel.width = 4;
    cel.height = 8;
    cel.transparency = 0;
    setLoadedCel(&loop, &cel, 0);
    memset(&unselected, 0, sizeof(unselected));
    unselected.bmp = view.codeBlock + 512;
    unselected.bitmapBank = view.codeBlockBank;
    unselected.width = 32;
    unselected.height = 64;
    unselected.splitSegments = 1;
    setLoadedCel(&loop, &unselected, 1);
    setLoadedLoop(&view, &loop, 0);
    setLoadedView(&view, 200);
    memCpyBanked(before, allocationArray, MEMORY_MANAGEMENT_BANK, sizeof(before));
    testPoke(0xA5, neighbor);
    inventoryActive = TRUE;
    showObjSeedTextOverlay();
    showObjCheckTextOverlay();
    /* Keep the surrounding modal display frozen through the call and probes;
     * ordinary input/status IRQs otherwise legitimately repaint their rows. */
    for (i=0; i<sizeof(reservedAttributes); ++i)
        reservedAttributes[i] = testPeek(0x1fff0UL+i);
    roomPixels[0] = testPeek(0);
    roomPixels[1] = testPeek(0x4800UL);
    roomPixels[2] = testPeek(0x95ffUL);
    /* Exercise DCSEL restoration after testPeek has selected its data port. */
    VERA.control = 3 << 1;
    previousControl = VERA.control;
    previousBank = RAM_BANK;
    previousIrq = testStatus() & 4;
    previousVideo = showObjDisplayVideo();
    auditCheck(VERA.control == previousControl && (testStatus() & 4) == previousIrq,
        "legacy show obj display-state sample changed VERA control or IRQ state");
    testPutKey('x');
    testVisibleFrames = 0;
    showObjCheckTextContext(after, TRUE);
    b11ShowObj(200);
    auditCheck(!sendIrqCommand,
        "legacy show obj returned with a pending IRQ command");
    bDResetSpriteTablePointer();
    showObjCheckTextContext(after, FALSE);
    returnedControl = VERA.control;
    returnedIrq = testStatus() & 4;
    returnedBank = RAM_BANK;
    returnedVideo = showObjDisplayVideo();
    auditCheck(VERA.control == returnedControl && returnedIrq == previousIrq &&
        (testStatus() & 4) == returnedIrq && returnedBank == previousBank,
        "legacy show obj changed VERA control, caller bank or IRQ state");
    /* DC_VIDEO bit 7 is the read-only current-field indicator and can toggle
     * while the overlay waits for visible frames. Bits 6:4 own sprite, L1,
     * and L0 output; bits 3:0 select sync/output mode. */
    auditCheck((returnedVideo & 0x7f) == (previousVideo & 0x7f),
        "legacy show obj did not restore writable successful-close DC_VIDEO state");
    getLoadedView(&returned, 200);
    auditCheck(returned.loaded && returned.codeBlock == view.codeBlock, "legacy show obj discarded a caller-owned loaded view");
    auditCheck(testPeek(selectedAddress) == 0x77 && testPeek(selectedAddress + 31) == 0x77,
        "LEGACY SHOW OBJ MUST RENDER SELECTED CEL WITH ONE 8X8 VRAM BLOCK FREE");
    auditCheck(testVisibleFrames != 0, "legacy show obj closed before a visible frame");
    auditCheck(!cbm_k_getin(), "legacy show obj did not consume its close event");
    auditCheck(testPeek(neighbor) == 0xA5, "legacy show obj overwrote neighboring allocated VRAM");
    for (i=0; i<sizeof(reservedAttributes); ++i)
        auditCheck(testPeek(0x1fff0UL+i) == reservedAttributes[i],
            "legacy show obj changed mouse or inspect attributes");
    auditCheck(testPeek(0) == roomPixels[0] &&
        testPeek(0x4800UL) == roomPixels[1] &&
        testPeek(0x95ffUL) == roomPixels[2],
        "legacy show obj changed room pixels");
    auditCheck(inventoryActive, "legacy show obj did not preserve the caller's modal state");
    getLoadedCel(&loop, &unselected, 1);
    auditCheck(unselected.width == 32 && unselected.height == 64 && unselected.bmp == view.codeBlock + 512,
        "selected legacy cel changed unselected cel metadata");
    memCpyBanked(inventoryBuffer, unselected.bmp, unselected.bitmapBank, 1);
    auditCheck(inventoryBuffer[0] == 0xA5, "selected legacy cel changed unselected cel resource");
    showObjCheckSelectedVramOwnership(0);
    showObjCheckTextOverlay();
    showObjSmallSaveAvailableCase();
    showObjWideCelCase(&view, &loop, &cel, neighbor);
    showObjCheckSelectedVramOwnership(0);
    showObjTallAuditStages(&view, &loop, &cel);
    memCpyBanked(after, allocationArray, MEMORY_MANAGEMENT_BANK, sizeof(after));
    auditCheck(!memcmp(before, after, sizeof(before)), "legacy show obj leaked save-under buffer");
    showObjCheckSpriteOwnership();
    showObjExhaustionCases();
    inventoryActive = FALSE;
    b9DiscardView(200);
    showObjFreshViewCases();
}
#pragma code-name (pop)
