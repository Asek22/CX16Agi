#include "audit.h"
#include "../../showObj.h"
#include "../../inventory.h"
#include <cbm.h>
#include <string.h>

extern byte allocationArray[], bDSpriteAllocTable[];
extern void testPutKey(byte key);
extern void testDisableInterrupts(void), testEnableInterrupts(void);
extern unsigned testVisibleFrames;
extern void setLoadedLoop(View* view, Loop* loop, byte number);
extern void setLoadedCel(Loop* loop, Cel* cel, byte number);
#pragma wrapped-call (push, trampoline, SPRITE_MEMORY_MANAGER_BANK)
extern void bDResetSpriteTablePointer(void);
#pragma wrapped-call (pop)
static byte before[ALLOCATION_ARRAY_SIZE], after[ALLOCATION_ARRAY_SIZE];
static byte attrs[8];
static byte pattern(unsigned address) { return (byte)(address ^ (address >> 8)); }
static void checkSpriteOwnership(void)
{
    unsigned i;
    byte saved = RAM_BANK;
    RAM_BANK = SPRITE_MEMORY_MANAGER_BANK;
    for (i=0; i<TOTAL_REAL_BLOCKS; ++i)
        auditCheck(!bDSpriteAllocTable[i], "legacy show obj left a bitmap allocation behind");
    RAM_BANK = saved;
}
static void exhaustionCases(void)
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
static void tallCelCase(View* view, Loop* loop, Cel* cel, byte height)
{
    byte row, rle[3] = {0x78, 0x08, 0};
    byte previousVideo, currentVideo, savedBank;
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
    savedBank = RAM_BANK;
    RAM_BANK = SPRITE_MEMORY_MANAGER_BANK;
    auditCheck(bDSpriteAllocTable[32] != 0, "legacy show obj released the live neighboring VRAM allocation");
    RAM_BANK = savedBank;
    bDDeleteAllocation(neighbor, SPR_SIZE_32, SPR_SIZE_64);
}
void showObjAuditCases(void)
{
    View view, returned;
    Loop loop;
    Cel cel;
    byte i, video;
    unsigned offset;
    byte bytes[28];
    memset(&view, 0, sizeof(view));
    memset(&loop, 0, sizeof(loop));
    memset(&cel, 0, sizeof(cel));
    view.loaded = TRUE;
    view.numberOfLoops = view.maxCels = view.maxVeraSlots = 1;
    view.loops = (Loop*)b10BankedAlloc(sizeof(loop), &view.loopsBank);
    loop.numberOfCels = 1;
    loop.cels = (Cel*)b10BankedAlloc(sizeof(cel), &loop.celsBank);
    loop.allocationWidth = SPR_ATTR_16;
    loop.allocationHeight = SPR_ATTR_8;
    /* Keep the real source bitmap in a different bank from its cel metadata. */
    view.codeBlock = b10BankedAlloc(1601, &view.codeBlockBank);
    auditCheck(view.codeBlockBank != loop.celsBank, "legacy cel fixture did not exercise a separate bitmap bank");
    for (i=0; i<16; ++i) bytes[i] = i&1 ? 0 : 0x78;
    memcpy(bytes+16, "an object", 10);
    memCpyBanked(view.codeBlock, bytes, view.codeBlockBank, 16);
    memCpyBanked(view.codeBlock+256, bytes+16, view.codeBlockBank, 10);
    view.description = (char*)view.codeBlock+256;
    cel.bmp = view.codeBlock;
    cel.bitmapBank = view.codeBlockBank;
    cel.width = cel.height = 8;
    cel.transparency = 0;
    setLoadedCel(&loop, &cel, 0);
    setLoadedLoop(&view, &loop, 0);
    setLoadedView(&view, 200);
    memCpyBanked(before, allocationArray, MEMORY_MANAGEMENT_BANK, sizeof(before));
    checkSpriteOwnership();
    bDResetSpriteMemoryManager();
    inventoryActive = TRUE;
    VERA.control = 0;
    video = VERA.display.video;
    for (offset=0; offset<4096; ++offset) testPoke(pattern(offset), MAPBASE+(unsigned long)offset);
    /* Keep the surrounding modal display frozen through the call and probes;
     * ordinary input/status IRQs otherwise legitimately repaint their rows. */
    for (i=0; i<8; ++i) attrs[i] = testPeek(0x1fff8UL+i);
    testVisibleFrames = 0;
    testPutKey('x');
    auditOutput("LEGACY SHOW OBJ MUST RETURN; ");
    b11ShowObj(200);
    getLoadedView(&returned, 200);
    auditCheck(returned.loaded && returned.codeBlock == view.codeBlock, "legacy show obj discarded a caller-owned loaded view");
    auditCheck(testVisibleFrames != 0, "legacy show obj closed before a visible frame");
    auditCheck(!cbm_k_getin(), "legacy show obj did not consume its close event");
    auditCheck(testPeek(VRAM_START) == 0x77, "legacy show obj did not render the source cel");
    VERA.control = 0;
    auditCheck(VERA.display.video == video, "legacy show obj did not restore display enable flags");
    for (i=0; i<8; ++i) auditCheck(testPeek(0x1fff8UL+i) == attrs[i], "legacy show obj left attributes pointing at freed VRAM");
    auditCheck(inventoryActive, "legacy show obj did not preserve the caller's modal state");
    for (offset=0; offset<4096; ++offset)
        auditCheck(testPeek(MAPBASE+(unsigned long)offset) == pattern(offset), "legacy show obj did not restore full text overlay");
    tallCelCase(&view, &loop, &cel, 64);
    tallCelCase(&view, &loop, &cel, 65);
    inventoryActive = FALSE;
    memCpyBanked(after, allocationArray, MEMORY_MANAGEMENT_BANK, sizeof(after));
    auditCheck(!memcmp(before, after, sizeof(before)), "legacy show obj leaked save-under buffer");
    checkSpriteOwnership();
    exhaustionCases();
    b9DiscardView(200);
}
