#include "audit.h"
#include "../../showObj.h"
#include "../../inventory.h"
#include <cbm.h>
#include <string.h>

extern byte allocationArray[], bDSpriteAllocTable[];
extern unsigned long inventoryAddress;
extern byte inventoryBuffer[80], inventoryLength, inventoryWrite;
extern void inventoryTransfer(void);
extern void testPutKey(byte key);
extern void testDisableInterrupts(void), testEnableInterrupts(void);
extern byte testStatus(void);
extern unsigned testVisibleFrames;
extern void setLoadedLoop(View* view, Loop* loop, byte number);
extern void setLoadedCel(Loop* loop, Cel* cel, byte number);
#pragma wrapped-call (push, trampoline, SPRITE_MEMORY_MANAGER_BANK)
extern void bDResetSpriteTablePointer(void);
#pragma wrapped-call (pop)
#pragma wrapped-call (push, trampoline, CONTROLLER_BANK)
void showObjExhaustionCases(void);
void showObjTallCelCase(View* view, Loop* loop, Cel* cel, byte height);
void showObjCheckTextOverlay(void);
void showObjSeedTextOverlay(void);
void showObjAuditCasesInBank(VeraSpriteAddress selectedAddress, VeraSpriteAddress neighbor);
#pragma wrapped-call (pop)
void showObjCheckSpriteOwnership(void);
void showObjCheckSelectedVramOwnership(void);
static byte before[ALLOCATION_ARRAY_SIZE], after[ALLOCATION_ARRAY_SIZE];
static byte attrs[8];
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
void showObjCheckSelectedVramOwnership(void)
{
    unsigned i;
    byte savedBank = RAM_BANK;
    RAM_BANK = SPRITE_MEMORY_MANAGER_BANK;
    for (i=0; i<TOTAL_REAL_BLOCKS; ++i) {
        if (bDSpriteAllocTable[i] != (i != 0)) {
            auditCheck(FALSE, "selected legacy cel changed surrounding VRAM ownership");
            break;
        }
    }
    RAM_BANK = savedBank;
}
static void showObjPrepareVramSlots(VeraSpriteAddress* selectedAddress, VeraSpriteAddress* neighbor)
{
    byte savedBank;
    bDResetSpriteMemoryManager();
    savedBank = RAM_BANK;
    RAM_BANK = SPRITE_MEMORY_MANAGER_BANK;
    /* Model all other live sprites without thousands of setup allocations. */
    memset(bDSpriteAllocTable, 1, TOTAL_REAL_BLOCKS);
    bDSpriteAllocTable[0] = bDSpriteAllocTable[1] = 0;
    RAM_BANK = savedBank;
    bDResetSpriteTablePointer();
    *selectedAddress = bDFindFreeVramBlock(SPR_SIZE_8, SPR_SIZE_8);
    *neighbor = bDFindFreeVramBlock(SPR_SIZE_8, SPR_SIZE_8);
    auditCheck(*selectedAddress == VRAM_START && *neighbor == VRAM_START + 32,
        "selected legacy cel exhaustion fixture did not reserve adjacent 8x8 blocks");
    bDDeleteAllocation(*selectedAddress, SPR_SIZE_8, SPR_SIZE_8);
    bDResetSpriteTablePointer();
}
#pragma code-name (push, "BANKRAM01")
static byte pattern(unsigned address) { return (byte)(address ^ (address >> 8)); }
static byte showObjDisplayVideo(void)
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
void showObjCheckTextOverlay(void)
{
    unsigned offset;
    for (offset=0; offset<4096; ++offset) {
        if (testPeek(MAPBASE+(unsigned long)offset) != pattern(offset)) {
            auditCheck(FALSE, "legacy show obj did not restore full text overlay");
            break;
        }
    }
}
void showObjSeedTextOverlay(void)
{
    unsigned offset;
    byte i;
    for (offset=0; offset<4096; offset+=64) {
        for (i=0; i<64; ++i) inventoryBuffer[i] = pattern(offset+i);
        inventoryAddress = MAPBASE+(unsigned long)offset;
        inventoryLength = 64;
        inventoryWrite = TRUE;
        inventoryTransfer();
    }
}
void showObjExhaustionCases(void)
{
    byte* blocks[LARGE_NO_SEGMENTS];
    byte banks[LARGE_NO_SEGMENTS], count = 0, i;
    View view;
    while (count < LARGE_NO_SEGMENTS) {
        blocks[count] = b10TryBankedAlloc(4096, &banks[count]);
        if (!blocks[count]) break;
        ++count;
    }
    auditCheck(!b10TryBankedAlloc(4096, &i), "legacy RAM exhaustion fixture left a save-under block free");
    b11ShowObj(200);
    getLoadedView(&view, 200);
    auditCheck(view.loaded, "legacy RAM exhaustion discarded a loaded view");
    while (count) { --count; b10BankedDealloc(blocks[count], banks[count]); }
    while (bDFindFreeVramBlock(SPR_SIZE_8, SPR_SIZE_8)) {}
    b11ShowObj(200);
    getLoadedView(&view, 200);
    auditCheck(view.loaded, "legacy VRAM exhaustion discarded a loaded view");
    bDResetSpriteMemoryManager();
    memCpyBanked(after, allocationArray, MEMORY_MANAGEMENT_BANK, sizeof(after));
    auditCheck(!memcmp(before, after, sizeof(before)), "legacy exhaustion leaked save-under allocation");
}
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
    auditCheck(currentVideo == previousVideo && inventoryActive && cbm_k_getin() == 'w',
        "oversized doubled legacy cel changed caller state");
    auditCheck(returned.loaded && returned.codeBlock == view->codeBlock,
        "oversized doubled legacy cel discarded its caller-owned view");
    auditCheck(testPeek(neighbor) == 0xA5, "oversized doubled legacy cel changed neighboring VRAM");
}
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
        auditCheck(currentVideo == previousVideo, "unsupported tall legacy cel changed display flags");
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
    byte previousIrq, returnedIrq;
    byte bytes[28];
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
    /* Keep the surrounding modal display frozen through the call and probes;
     * ordinary input/status IRQs otherwise legitimately repaint their rows. */
    for (i=0; i<8; ++i) attrs[i] = testPeek(0x1fff8UL+i);
    /* Exercise DCSEL restoration after testPeek has selected its data port. */
    VERA.control = 3 << 1;
    previousControl = VERA.control;
    previousIrq = testStatus() & 4;
    previousVideo = showObjDisplayVideo();
    auditCheck(VERA.control == previousControl && (testStatus() & 4) == previousIrq,
        "legacy show obj display-state sample changed VERA control or IRQ state");
    testPutKey('x');
    testVisibleFrames = 0;
    b11ShowObj(200);
    returnedControl = VERA.control;
    returnedIrq = testStatus() & 4;
    returnedVideo = showObjDisplayVideo();
    auditCheck(VERA.control == returnedControl && returnedIrq == previousIrq &&
        (testStatus() & 4) == returnedIrq,
        "legacy show obj changed VERA control or the caller's IRQ state");
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
    for (i=0; i<8; ++i) auditCheck(testPeek(0x1fff8UL+i) == attrs[i], "legacy show obj left attributes pointing at freed VRAM");
    auditCheck(inventoryActive, "legacy show obj did not preserve the caller's modal state");
    getLoadedCel(&loop, &unselected, 1);
    auditCheck(unselected.width == 32 && unselected.height == 64 && unselected.bmp == view.codeBlock + 512,
        "selected legacy cel changed unselected cel metadata");
    memCpyBanked(bytes+27, unselected.bmp, unselected.bitmapBank, 1);
    auditCheck(bytes[27] == 0xA5, "selected legacy cel changed unselected cel resource");
    showObjCheckSelectedVramOwnership();
    showObjCheckTextOverlay();
    showObjWideCelCase(&view, &loop, &cel, neighbor);
    showObjCheckSelectedVramOwnership();
    showObjTallCelCase(&view, &loop, &cel, 64);
    showObjTallCelCase(&view, &loop, &cel, 65);
    inventoryActive = FALSE;
    memCpyBanked(after, allocationArray, MEMORY_MANAGEMENT_BANK, sizeof(after));
    auditCheck(!memcmp(before, after, sizeof(before)), "legacy show obj leaked save-under buffer");
    showObjCheckSpriteOwnership();
    showObjExhaustionCases();
    b9DiscardView(200);
}
#pragma code-name (pop)
