#include "audit.h"
#include "../../showObj.h"
#include "../../inventory.h"
#include <cbm.h>
#include <string.h>

extern byte allocationArray[], bDSpriteAllocTable[];
extern void testPutKey(byte key);
extern unsigned testVisibleFrames;
extern void setLoadedLoop(View* view, Loop* loop, byte number);
extern void setLoadedCel(Loop* loop, Cel* cel, byte number);
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
    /* Keep the real source bitmap in a different bank from its cel metadata. */
    view.codeBlock = b10BankedAlloc(1601, &view.codeBlockBank);
    auditCheck(view.codeBlockBank != loop.celsBank, "legacy cel fixture did not exercise a separate bitmap bank");
    for (i=0; i<16; ++i) bytes[i] = i&1 ? 0 : 0x78;
    memcpy(bytes+16, "an object", 10);
    memCpyBanked(view.codeBlock, bytes, view.codeBlockBank, sizeof(bytes));
    view.description = (char*)view.codeBlock+16;
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
    inventoryActive = FALSE;
    memCpyBanked(after, allocationArray, MEMORY_MANAGEMENT_BANK, sizeof(after));
    auditCheck(!memcmp(before, after, sizeof(before)), "legacy show obj leaked save-under buffer");
    checkSpriteOwnership();
    exhaustionCases();
    b9DiscardView(200);
}
