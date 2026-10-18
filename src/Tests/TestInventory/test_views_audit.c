#include "audit.h"
#include "../../view.h"
#include "../../agifiles.h"
#include "../../memoryManager.h"
#include "../../paletteManager.h"
#include <stdio.h>
#include <string.h>
extern byte viewHeaderBuffer[], loopHeaderBuffer[];
extern byte allocationArray[], palettesAllocated;
extern int allocatedPaletteOwners[];
#pragma wrapped-call (push, trampoline, VIEW_CODE_BANK_1)
extern void setViewData(byte number, AGIFile* file, View* view);
extern void setLoopData(AGIFile* file, View* view, Loop* loop, byte* header, byte number, byte loopNum);
#pragma wrapped-call (pop)
#pragma wrapped-call (push, trampoline, SPRITE_METADATA_BANK)
extern boolean bESetViewMetadata(View* view, ViewTable* table, byte viewNum, byte tableNum, byte slot);
extern void bEResetViewTableMetadata(void), bEResetSpritesUpdatedBuffer(void), bEResetSpritePointers(void);
#pragma wrapped-call (pop)
#pragma wrapped-call (push, trampoline, LINE_DRAW_BANK)
extern void b8SetupLineTables(void);
#pragma wrapped-call (pop)
extern boolean agiBlit(byte entry, boolean disableInterrupts);
extern byte bESpritesUpdatedBuffer[];
extern void testDisableInterrupts(void), testEnableInterrupts(void);

#pragma wrapped-call (push, trampoline, 0x01)
static void writeViewVolume(const byte* resource, unsigned int size);
static void setViewDirectory(AGIFilePosType* previous);
static void rejectView(const byte* resource, unsigned int size, const char* reason);
static void oneCelResource(byte* resource, byte width, byte height, byte transparency,
                           const byte* rle, byte rleSize);
static void writeZeroes(FILE* output, unsigned int count);
#pragma wrapped-call (pop)
#pragma wrapped-call (push, trampoline, 0x02)
static void viewsAuditCasesBanked(void);
#pragma wrapped-call (pop)
#pragma wrapped-call (push, trampoline, 0x07)
static void multiloopLoadWorkload(void);
#pragma wrapped-call (pop)
#pragma wrapped-call (push, trampoline, 0x08)
static void bankEdgeViewCase(byte terminated);
#pragma wrapped-call (pop)
#pragma wrapped-call (push, trampoline, 0x0f)
static void largeViewResourceCases(void);
#pragma wrapped-call (pop)
#pragma wrapped-call (push, trampoline, 0x0f)
static void viewRleCases(void);
#pragma wrapped-call (pop)
#pragma wrapped-call (push, trampoline, 0x04)
static void structureBoundCases(void);
#pragma wrapped-call (pop)
#pragma wrapped-call (push, trampoline, 0x09)
static void validViewRleCase(void);
#pragma wrapped-call (pop)
#pragma wrapped-call (push, trampoline, 0x02)
static void mirroredViewCase(void);
static void denseViewCase(void);
static void writeValidViewFixture(void);
#pragma wrapped-call (pop)
#pragma wrapped-call (push, trampoline, 0x12)
static void mapAllocationPressureCase(byte exhaustSmallPools);
#pragma wrapped-call (pop)
#pragma wrapped-call (push, trampoline, 0x12)
static void prepareMapPressurePools(byte exhaustSmallPools);
#pragma wrapped-call (pop)
static void runRequiredMapPressureFailure(void);
#pragma wrapped-call (push, trampoline, 0x04)
static void runSmallViewPressureSuccess(void);
#pragma wrapped-call (pop)
#pragma wrapped-call (push, trampoline, 0x0f)
static void writeLargeViewVolume(unsigned int size, byte valid);
#pragma wrapped-call (pop)
#pragma wrapped-call (push, trampoline, 0x08)
static void writeBankEdgeView(byte terminated);
#pragma wrapped-call (pop)
#pragma wrapped-call (push, trampoline, 0x0f)
static void viewHeaderCase(byte loops);
#pragma wrapped-call (pop)
#pragma wrapped-call (push, trampoline, 0x07)
static void loopHeaderCase(void);
#pragma wrapped-call (pop)

#pragma code-name (push, "BANKRAM01")
#pragma rodata-name (push, "BANKRAM01")
#pragma bss-name (push, "BSS")

#define VIEW_AUDIT_NUMBER 253
#define VIEW_PALETTE_BYTES (COLOURS_PER_PALETTE * BYTES_PER_PALETTE_COLOUR)
#define HIGH_ALIAS_LOOPS 100
#define HIGH_ALIAS_HEADER_SIZE (5 + HIGH_ALIAS_LOOPS * 2)
#define VIEW_MULTILOOP_BASELINE_AVERAGE 18261833UL
#define VIEW_MULTILOOP_BASELINE_WORST 18266971UL
#define VIEW_MULTILOOP_CYCLE_LIMIT (VIEW_MULTILOOP_BASELINE_AVERAGE / 2)
static byte allocationBefore[ALLOCATION_ARRAY_SIZE], allocationAfter[ALLOCATION_ARRAY_SIZE];
static byte paletteBefore[(NO_MANAGED_PALETTES) * sizeof(int)], paletteAfter[(NO_MANAGED_PALETTES) * sizeof(int)];
static byte paletteVramBefore[VIEW_PALETTE_BYTES], paletteVramAfter[VIEW_PALETTE_BYTES];
static byte pictureBefore[16], pictureAfter[16];
static byte* mapPressureBlocks[EXTRA_SMALL_NO_SEGMENTS + SMALL_NO_SEGMENTS +
    MEDIUM_NO_SEGMENTS + LARGE_NO_SEGMENTS];
static byte mapPressureBanks[EXTRA_SMALL_NO_SEGMENTS + SMALL_NO_SEGMENTS +
    MEDIUM_NO_SEGMENTS + LARGE_NO_SEGMENTS];
static byte mapPressureCount, mapPressureBank, mapPressureSmallBank, mapPressureSmallIndex;
#pragma bss-name (pop)
#pragma bss-name (push, "BANKRAM0F")
static byte viewHeaderBytes[515];
#pragma bss-name (pop)
#pragma bss-name (push, "BSS")
static byte loopHeaderBytes[511];
#pragma bss-name (pop)
#pragma bss-name (push, "BANKRAM01")

#pragma wrapped-call (push, trampoline, 0x0f)
extern boolean b9ValidateViewRleForInventoryTest(AGIFile* tempAGI);
#pragma wrapped-call (pop)

static void writeViewVolume(const byte* resource, unsigned int size)
{
    byte header[5] = {0x12, 0x34, 0, 0, 0};
    FILE* output = fopen("vol.8", "wb");
    auditCheck(output != 0, "VIEW RLE fixture creation");
    header[3] = (byte)size;
    header[4] = (byte)(size >> 8);
    auditCheck(fwrite(header, 1, sizeof(header), output) == sizeof(header), "VIEW RLE fixture header write");
    auditCheck(fwrite(resource, 1, size, output) == size, "VIEW RLE fixture write");
    auditCheck(fclose(output) == 0, "VIEW RLE fixture close");
}

static void setViewDirectory(AGIFilePosType* previous)
{
    AGIFilePosType position;
    memCpyBanked((byte*)previous, (byte*)&viewdir[VIEW_AUDIT_NUMBER], DIRECTORY_BANK, sizeof(*previous));
    memset(&position, 0, sizeof(position));
    position.fileNum = 8;
    memCpyBanked((byte*)&viewdir[VIEW_AUDIT_NUMBER], (byte*)&position, DIRECTORY_BANK, sizeof(position));
}

static void rejectView(const byte* resource, unsigned int size, const char* reason)
{
    AGIFilePosType previous;
    View before, after;
    byte paletteCount, paletteCountAfter, previousBank = RAM_BANK;
    byte loaded, cacheUnchanged, allocationsReleased, paletteUnchanged, vramUnchanged;
    unsigned long paletteAddress;
    unsigned int i;

    if (resource) writeViewVolume(resource, size);
    else writeLargeViewVolume(size, TRUE);
    setViewDirectory(&previous);
    getLoadedView(&before, VIEW_AUDIT_NUMBER);
    memCpyBankedBetween(allocationBefore, 0x01, allocationArray, MEMORY_MANAGEMENT_BANK, sizeof(allocationBefore));
    memCpyBankedBetween(paletteBefore, 0x01, (byte*)allocatedPaletteOwners, PALETTE_MANAGER_BANK, sizeof(paletteBefore));
    memCpyBanked(&paletteCount, (byte*)&palettesAllocated, PALETTE_MANAGER_BANK, 1);
    paletteAddress = paletteCount < NO_MANAGED_PALETTES ?
        PALETTE_START + (BASE_MANAGED_PALETTE + paletteCount) * VIEW_PALETTE_BYTES : PALETTE_START;
    for (i = 0; i < VIEW_PALETTE_BYTES; ++i) paletteVramBefore[i] = testPeek(paletteAddress + i);
    for (i = 0; i < sizeof(pictureBefore); ++i) pictureBefore[i] = testPeek(STARTING_BYTE + i);

    loaded = b9TryLoadViewFile(VIEW_AUDIT_NUMBER);
    getLoadedView(&after, VIEW_AUDIT_NUMBER);
    cacheUnchanged = !memcmp(&before, &after, sizeof(before));
    memCpyBankedBetween(allocationAfter, 0x01, allocationArray, MEMORY_MANAGEMENT_BANK, sizeof(allocationAfter));
    allocationsReleased = !memcmp(allocationBefore, allocationAfter, sizeof(allocationBefore));
    memCpyBankedBetween(paletteAfter, 0x01, (byte*)allocatedPaletteOwners, PALETTE_MANAGER_BANK, sizeof(paletteAfter));
    memCpyBanked(&paletteCountAfter, (byte*)&palettesAllocated, PALETTE_MANAGER_BANK, 1);
    paletteUnchanged = paletteCount == paletteCountAfter && !memcmp(paletteBefore, paletteAfter, sizeof(paletteBefore));
    for (i = 0; i < VIEW_PALETTE_BYTES; ++i) {
        paletteVramAfter[i] = testPeek(paletteAddress + i);
        if (paletteVramBefore[i] != paletteVramAfter[i]) paletteUnchanged = FALSE;
    }
    for (i = 0; i < sizeof(pictureAfter); ++i) {
        pictureAfter[i] = testPeek(STARTING_BYTE + i);
    }
    vramUnchanged = !memcmp(pictureBefore, pictureAfter, sizeof(pictureBefore));
    if (loaded) b9DiscardView(VIEW_AUDIT_NUMBER);
    memCpyBanked((byte*)&viewdir[VIEW_AUDIT_NUMBER], (byte*)&previous, DIRECTORY_BANK, sizeof(previous));
    auditCheck(remove("vol.8") == 0, "VIEW RLE fixture cleanup");

    auditCheck(!loaded, reason);
    auditCheck(cacheUnchanged, "rejected VIEW published loaded cache metadata");
    auditCheck(allocationsReleased, "rejected VIEW leaked its resource, loop or cel allocation");
    auditCheck(paletteUnchanged, "rejected VIEW changed sprite palette ownership or VRAM");
    auditCheck(vramUnchanged, "rejected VIEW changed neighboring picture VRAM");
    auditCheck(RAM_BANK == previousBank, "rejected VIEW changed the caller RAM bank");
}

static void oneCelResource(byte* resource, byte width, byte height, byte transparency,
                           const byte* rle, byte rleSize)
{
    memset(resource, 0, 13 + rleSize);
    resource[2] = 1;
    resource[5] = 7;
    resource[7] = 1;
    resource[8] = 3;
    resource[10] = width;
    resource[11] = height;
    resource[12] = transparency;
    memcpy(resource + 13, rle, rleSize);
}

static void writeZeroes(FILE* output, unsigned int count)
{
    byte zeros[128];
    memset(zeros, 0, sizeof(zeros));
    while (count) {
        unsigned int amount = count < sizeof(zeros) ? count : sizeof(zeros);
        auditCheck(fwrite(zeros, 1, amount, output) == amount, "bank-edge VIEW padding write");
        count -= amount;
    }
}

#pragma code-name (push, "BANKRAM0F")
#pragma rodata-name (push, "BANKRAM0F")
static void writeLargeViewVolume(unsigned int size, byte valid)
{
    byte header[5] = {0x12, 0x34, 0, 0, 0};
    byte resource[15] = {0};
    FILE* output = fopen("vol.8", "wb");
    auditCheck(size >= sizeof(resource), "large VIEW fixture size");
    header[3] = (byte)size;
    header[4] = (byte)(size >> 8);
    resource[2] = 1;
    resource[5] = 7;
    resource[7] = 1;
    resource[8] = 3;
    resource[10] = 1;
    resource[11] = 1;
    resource[13] = valid ? 0x11 : 0x22;
    auditCheck(output != 0, "large VIEW fixture creation");
    auditCheck(fwrite(header, 1, sizeof(header), output) == sizeof(header), "large VIEW volume header write");
    auditCheck(fwrite(resource, 1, sizeof(resource), output) == sizeof(resource), "large VIEW resource write");
    writeZeroes(output, size - sizeof(resource));
    auditCheck(fclose(output) == 0, "large VIEW fixture close");
}

static void largeViewResourceCase(unsigned int size, byte valid)
{
    AGIFilePosType previous;
    byte loaded, paletteCountBefore, paletteCountAfter;

    memCpyBankedBetween(allocationBefore, 0x01, allocationArray,
        MEMORY_MANAGEMENT_BANK, sizeof(allocationBefore));
    memCpyBankedBetween(paletteBefore, 0x01, (byte*)allocatedPaletteOwners,
        PALETTE_MANAGER_BANK, sizeof(paletteBefore));
    memCpyBanked(&paletteCountBefore, (byte*)&palettesAllocated,
        PALETTE_MANAGER_BANK, 1);

    writeLargeViewVolume(size, valid);
    setViewDirectory(&previous);
    loaded = b9TryLoadViewFile(VIEW_AUDIT_NUMBER);
    if (loaded) b9DiscardView(VIEW_AUDIT_NUMBER);
    memCpyBanked((byte*)&viewdir[VIEW_AUDIT_NUMBER], (byte*)&previous, DIRECTORY_BANK, sizeof(previous));
    memCpyBankedBetween(allocationAfter, 0x01, allocationArray,
        MEMORY_MANAGEMENT_BANK, sizeof(allocationAfter));
    memCpyBankedBetween(paletteAfter, 0x01, (byte*)allocatedPaletteOwners,
        PALETTE_MANAGER_BANK, sizeof(paletteAfter));
    memCpyBanked(&paletteCountAfter, (byte*)&palettesAllocated,
        PALETTE_MANAGER_BANK, 1);
    auditCheck(remove("vol.8") == 0, "large VIEW fixture cleanup");
    auditCheck(loaded == valid, valid ? "large valid VIEW was rejected" : "large malformed VIEW was accepted");
    auditCheck(!memcmp(allocationBefore, allocationAfter, sizeof(allocationBefore)),
        "large VIEW load or rejection leaked banked RAM");
    if (!valid) auditCheck(paletteCountBefore == paletteCountAfter &&
        !memcmp(paletteBefore, paletteAfter, sizeof(paletteBefore)),
        "large malformed VIEW changed palette ownership");
}

static void largeViewResourceCases(void)
{
    largeViewResourceCase(1024, TRUE);
    largeViewResourceCase(1024, FALSE);
    largeViewResourceCase(2048, TRUE);
    largeViewResourceCase(2048, FALSE);
    largeViewResourceCase(8000, TRUE);
    largeViewResourceCase(8000, FALSE);
}

static void highAliasViewCase(void)
{
    enum { LOOP_START = HIGH_ALIAS_HEADER_SIZE };
    byte highAliasResource[HIGH_ALIAS_HEADER_SIZE + 8];
    byte i, loaded;
    AGIFilePosType previous;
    View view;
    Loop loop;
    Cel cel;

    memset(highAliasResource, 0, sizeof(highAliasResource));
    highAliasResource[2] = HIGH_ALIAS_LOOPS;
    for (i = 0; i < HIGH_ALIAS_LOOPS; ++i) {
        highAliasResource[5 + (unsigned int)i * 2] = LOOP_START;
        highAliasResource[6 + (unsigned int)i * 2] = LOOP_START >> 8;
    }
    highAliasResource[LOOP_START] = 1;
    highAliasResource[LOOP_START + 1] = 3;
    highAliasResource[LOOP_START + 3] = 1;
    highAliasResource[LOOP_START + 4] = 1;
    highAliasResource[LOOP_START + 6] = 0x11;
    writeViewVolume(highAliasResource, sizeof(highAliasResource));
    setViewDirectory(&previous);
    memCpyBankedBetween(allocationBefore, 0x01, allocationArray,
        MEMORY_MANAGEMENT_BANK, sizeof(allocationBefore));
    loaded = b9TryLoadViewFile(VIEW_AUDIT_NUMBER);
    auditCheck(loaded, "VIEW loader rejected 100 aliases to one valid loop");
    getLoadedView(&view, VIEW_AUDIT_NUMBER);
    auditCheck(view.numberOfLoops == HIGH_ALIAS_LOOPS, "VIEW loader lost aliased loop count");
    getLoadedLoop(&view, &loop, HIGH_ALIAS_LOOPS - 1);
    getLoadedCel(&loop, &cel, 0);
    auditCheck(cel.width == 1 && cel.height == 1,
        "last aliased loop lost its valid cel");
    b9DiscardView(VIEW_AUDIT_NUMBER);
    memCpyBankedBetween(allocationAfter, 0x01, allocationArray,
        MEMORY_MANAGEMENT_BANK, sizeof(allocationAfter));
    memCpyBanked((byte*)&viewdir[VIEW_AUDIT_NUMBER], (byte*)&previous,
        DIRECTORY_BANK, sizeof(previous));
    auditCheck(remove("vol.8") == 0, "aliased VIEW fixture cleanup");
    auditCheck(!memcmp(allocationBefore, allocationAfter, sizeof(allocationBefore)),
        "aliased VIEW load leaked banked RAM");
}

#pragma code-name (push, "BANKRAM12")
#pragma rodata-name (push, "BANKRAM12")
static void mapAllocationPressureCase(byte exhaustSmallPools)
{
    byte oldBank = RAM_BANK;
    prepareMapPressurePools(exhaustSmallPools);
    if (exhaustSmallPools) runRequiredMapPressureFailure();
    else runSmallViewPressureSuccess();
    while (mapPressureCount) {
        --mapPressureCount;
        if (exhaustSmallPools && mapPressureCount == mapPressureSmallIndex) continue;
        auditCheck(b10BankedDealloc(mapPressureBlocks[mapPressureCount],
            mapPressureBanks[mapPressureCount]),
            "VIEW map pressure allocation cleanup");
    }
    auditCheck(RAM_BANK == oldBank, "VIEW map pressure changed the caller RAM bank");
}
#pragma rodata-name (pop)
#pragma code-name (pop)

#pragma code-name (push, "BANKRAM12")
#pragma rodata-name (push, "BANKRAM12")
static void prepareMapPressurePools(byte exhaustSmallPools)
{
    byte* block;
    unsigned int pressureSize = exhaustSmallPools ? EXTRA_SMALL_SIZE : MEDIUM_SIZE;

    mapPressureCount = 0;
    mapPressureSmallIndex = 255;
    while (mapPressureCount < sizeof(mapPressureBanks)) {
        block = b10TryBankedAlloc(pressureSize, &mapPressureBank);
        if (!block) break;
        if (mapPressureBank >= SMALL_FIRST_BANK &&
            mapPressureBank < SMALL_FIRST_BANK + SMALL_NO_BANKS && mapPressureSmallIndex == 255) {
            mapPressureSmallIndex = mapPressureCount;
            mapPressureSmallBank = mapPressureBank;
        }
        mapPressureBlocks[mapPressureCount] = block;
        mapPressureBanks[mapPressureCount++] = mapPressureBank;
    }
    auditCheck(!b10TryBankedAlloc(pressureSize, &mapPressureBank),
        "VIEW map pressure did not exhaust the requested allocation pools");
    if (exhaustSmallPools) {
        auditCheck(mapPressureSmallIndex != 255,
            "VIEW map pressure left no small resource staging block");
        auditCheck(b10BankedDealloc(mapPressureBlocks[mapPressureSmallIndex],
            mapPressureSmallBank), "VIEW map staging block cleanup");
    }
    block = b10TryBankedAlloc(exhaustSmallPools ? 401 : 15, &mapPressureBank);
    auditCheck(block != 0, "VIEW map pressure exhausted resource staging capacity");
    auditCheck(b10BankedDealloc(block, mapPressureBank), "VIEW map pressure probe cleanup");
}
#pragma rodata-name (pop)
#pragma code-name (pop)

#pragma code-name (push, "BANKRAM12")
#pragma rodata-name (push, "BANKRAM12")
static void runRequiredMapPressureFailure(void)
{
    rejectView(0, 401,
        "VIEW LOADER DID NOT CLEAN UP AFTER A REQUIRED STRUCTURE-MAP ALLOCATION FAILED");
}

#pragma rodata-name (pop)
#pragma code-name (pop)

#pragma code-name (push, "BANKRAM04")
#pragma rodata-name (push, "BANKRAM04")
static void runSmallViewPressureSuccess(void)
{
    byte loaded;
    AGIFilePosType previous;

    writeLargeViewVolume(15, TRUE);
    setViewDirectory(&previous);
    loaded = b9TryLoadViewFile(VIEW_AUDIT_NUMBER);
    auditCheck(loaded, "small valid VIEW failed with medium and large pools exhausted");
    b9DiscardView(VIEW_AUDIT_NUMBER);
    memCpyBanked((byte*)&viewdir[VIEW_AUDIT_NUMBER], (byte*)&previous,
        DIRECTORY_BANK, sizeof(previous));
    auditCheck(remove("vol.8") == 0, "VIEW map pressure fixture cleanup");
}
#pragma rodata-name (pop)
#pragma code-name (pop)
#pragma rodata-name (pop)
#pragma code-name (pop)

#pragma code-name (push, "BANKRAM08")
#pragma rodata-name (push, "BANKRAM08")
static void writeBankEdgeView(byte terminated)
{
    byte volumeHeader[5] = {0x12, 0x34, 0, 0x40, 0x1f};
    byte viewHeader[10] = {0};
    byte celHeader[3] = {1, 1, 0};
    byte run = 0x11, end = 0;
    unsigned int cellOffset = terminated ? 7988 : 7989;
    unsigned int cellStart = terminated ? 7995 : 7996;
    FILE* output;

    viewHeader[2] = 1;
    viewHeader[5] = 7;
    viewHeader[7] = 1;
    viewHeader[8] = (byte)cellOffset;
    viewHeader[9] = (byte)(cellOffset >> 8);
    output = fopen("vol.8", "wb");
    auditCheck(output != 0, "bank-edge VIEW fixture creation");
    auditCheck(fwrite(volumeHeader, 1, sizeof(volumeHeader), output) == sizeof(volumeHeader), "bank-edge VIEW volume header write");
    auditCheck(fwrite(viewHeader, 1, sizeof(viewHeader), output) == sizeof(viewHeader), "bank-edge VIEW header write");
    writeZeroes(output, cellStart - sizeof(viewHeader));
    auditCheck(fwrite(celHeader, 1, sizeof(celHeader), output) == sizeof(celHeader), "bank-edge VIEW cel header write");
    auditCheck(fwrite(&run, 1, 1, output) == 1, "bank-edge VIEW final run write");
    if (terminated) auditCheck(fwrite(&end, 1, 1, output) == 1, "bank-edge VIEW final terminator write");
    auditCheck(fclose(output) == 0, "bank-edge VIEW fixture close");
}

#pragma rodata-name (pop)
#pragma code-name (pop)
#pragma code-name (push, "BANKRAM08")
#pragma rodata-name (push, "BANKRAM08")

static void bankEdgeViewCase(byte terminated)
{
    AGIFilePosType previous;
    unsigned long address;
    byte bank, loaded, i;
    byte zero = 0;

    writeBankEdgeView(terminated);
    setViewDirectory(&previous);
    for (bank = LARGE_FIRST_BANK; bank < LARGE_FIRST_BANK + LARGE_NO_BANKS; ++bank)
        memCpyBanked((byte*)(BANK_MIN + MEMORY_MANAGER_BANK_SIZE), &zero, bank, 1);
    loaded = b9TryLoadViewFile(VIEW_AUDIT_NUMBER);
    if (terminated && loaded) {
        b8SetupLineTables();
        address = b8GetVeraPictureAddress(10, 5);
        testPoke(0x44, address);
        testPoke(0x44, address + 1);
        for (i = 0; i < 80; ++i) testPoke(0x44, PRIORITY_START + 5UL * 80 + i);
        testDisableInterrupts();
        b9AddToPic(VIEW_AUDIT_NUMBER, 0, 0, 10, 5, 15, 15);
        testEnableInterrupts();
        auditCheck(testPeek(address) == 0x11 && testPeek(address + 1) == 0x44,
                   "bank-edge VIEW did not render its final terminated pixel");
        b9DiscardView(VIEW_AUDIT_NUMBER);
    }
    memCpyBanked((byte*)&viewdir[VIEW_AUDIT_NUMBER], (byte*)&previous, DIRECTORY_BANK, sizeof(previous));
	auditCheck(remove("vol.8") == 0, "bank-edge VIEW fixture cleanup");
    auditCheck(loaded == terminated, terminated ? "exact allocation-end VIEW was rejected" :
               "VIEW read past its 8000-byte bank allocation for a missing row terminator");
}
#pragma rodata-name (pop)
#pragma code-name (pop)
#pragma code-name (push, "BANKRAM0F")
#pragma rodata-name (push, "BANKRAM0F")
static void viewRleCases(void)
{
    byte resource[64];
    byte run[] = {0x11};
    byte truncated[] = {0x11, 0, 0x11};
    byte oversized[] = {0x13, 0};
    byte zeroRun[] = {0x10, 0};

    largeViewResourceCases();
    oneCelResource(resource, 1, 1, 0, run, sizeof(run));
    rejectView(resource, 14, "VIEW LOADER ACCEPTED A CEL ROW WITHOUT ITS TERMINATOR");
    oneCelResource(resource, 1, 2, 0, truncated, sizeof(truncated));
    rejectView(resource, 16, "VIEW LOADER ACCEPTED A TRUNCATED SECOND CEL ROW");
    oneCelResource(resource, 2, 1, 0, oversized, sizeof(oversized));
    rejectView(resource, 15, "VIEW LOADER ACCEPTED RUNS WIDER THAN THE CEL");
    oneCelResource(resource, 1, 1, 0, zeroRun, sizeof(zeroRun));
    rejectView(resource, 15, "VIEW LOADER ACCEPTED A NONTERMINATOR ZERO-LENGTH RUN");

    structureBoundCases();
    highAliasViewCase();
    mapAllocationPressureCase(FALSE);
    mapAllocationPressureCase(TRUE);
    multiloopLoadWorkload();
}
#pragma rodata-name (pop)
#pragma code-name (pop)

#pragma code-name (push, "BANKRAM02")
#pragma rodata-name (push, "BANKRAM02")
static void mirroredViewCase(void)
{
    View view;
    Loop loop;
    Cel cel;
    ViewTable table;
    byte update[SPRITE_UPDATE_RECORD_SIZE], row, loaded;
    unsigned long spriteAddress;

    getLoadedView(&view, VIEW_AUDIT_NUMBER);
    getLoadedLoop(&view, &loop, 0);
    getLoadedCel(&loop, &cel, 1);
    auditCheck(cel.flipped, "valid mirrored cel lost its flip metadata");
    memset(&table, 0, sizeof(table));
    table.currentView = VIEW_AUDIT_NUMBER; table.currentLoop = 0; table.currentCel = 1;
    table.xPos = 10; table.yPos = 40; table.priority = 15;
    setViewTab(&table, 0);
    bEResetViewTableMetadata(); bEResetSpritePointers(); bEResetSpritesUpdatedBuffer();
    auditCheck(bESetViewMetadata(&view, &table, VIEW_AUDIT_NUMBER, 0, 0), "valid mirrored VIEW metadata allocation failed");
    bDResetSpriteMemoryManager();
    b8SetupLineTables();
    for (row = 0; row < 80; ++row) testPoke(0x44, PRIORITY_START + 40UL * 80 + row);
    testDisableInterrupts();
    loaded = agiBlit(0, FALSE);
    memCpyBanked(update, bESpritesUpdatedBuffer, SPRITE_UPDATED_BANK, sizeof(update));
    testEnableInterrupts();
    auditCheck(loaded && (update[5] & 1), "valid mirrored VIEW did not render with the hardware flip attribute");
    spriteAddress = ((unsigned long)update[0] | ((unsigned long)update[1] << 8)) * 32;
    auditCheck(testPeek(spriteAddress) == 0x11 && testPeek(spriteAddress + 1) == 0x11 &&
               testPeek(spriteAddress + 2) == 0x22, "valid mirrored VIEW lost its rendered RLE pixels");
    bEResetSpritesUpdatedBuffer(); bEResetViewTableMetadata(); bDResetSpriteMemoryManager();
}
static void denseViewCase(void)
{
    View view;
    Loop loop;
    Cel cel;
    byte i;
    unsigned long address;
    getLoadedView(&view, VIEW_AUDIT_NUMBER);
    getLoadedLoop(&view, &loop, 0);
    getLoadedCel(&loop, &cel, 2);
    auditCheck(cel.width == 8 && !cel.flipped, "valid dense cel metadata changed");
    address = b8GetVeraPictureAddress(20, 30);
    for (i = 0; i < 8; ++i) testPoke(0x44, address + i);
    b9AddToPic(VIEW_AUDIT_NUMBER, 0, 2, 20, 30, 15, 15);
    for (i = 0; i < 8; ++i) auditCheck(testPeek(address + i) == 0x11, "valid dense VIEW row lost a rendered pixel");
}
static void writeValidViewFixture(void)
{
    byte resource[38];
    byte i;
    memset(resource, 0, sizeof(resource));
    resource[2] = 1; resource[5] = 7;
    resource[7] = 3; resource[8] = 7; resource[10] = 13; resource[12] = 19;
    resource[14] = 3; resource[15] = 2;
    resource[17] = 0x12; resource[18] = 0; resource[19] = 0;
    resource[20] = 3; resource[21] = 1; resource[22] = 0x90;
    resource[23] = 0x12; resource[24] = 0x21; resource[25] = 0;
    resource[26] = 8; resource[27] = 1;
    for (i = 0; i < 8; ++i) resource[29 + i] = 0x11;
    resource[37] = 0;
    writeViewVolume(resource, sizeof(resource));
}
#pragma rodata-name (pop)
#pragma code-name (pop)
#pragma code-name (push, "BANKRAM04")
#pragma rodata-name (push, "BANKRAM04")
static void structureBoundCases(void)
{
    byte resource[64];
    memset(resource, 0, 21);
    resource[2] = 1; resource[5] = 7;
    resource[7] = 2; resource[8] = 5; resource[10] = 9;
    resource[12] = 20; resource[13] = 1; resource[15] = 0x11;
    resource[16] = 1; resource[17] = 1; resource[19] = 0x11; resource[20] = 0;
    rejectView(resource, 21, "VIEW CEL READ A NEIGHBOR CEL HEADER AS ITS ROW TERMINATOR");

    memset(resource, 0, 24);
    resource[2] = 2; resource[5] = 9; resource[7] = 16;
    resource[9] = 1; resource[10] = 3; resource[12] = 20; resource[13] = 1; resource[15] = 0x11;
    resource[16] = 1; resource[17] = 3; resource[19] = 1; resource[20] = 1; resource[22] = 0x11; resource[23] = 0;
    rejectView(resource, 24, "VIEW CEL READ A NEIGHBOR LOOP HEADER AS ITS ROW TERMINATOR");

    {
        byte run = 0x11;
        oneCelResource(resource, 20, 1, 0, &run, 1);
    }
    resource[3] = 14;
    resource[14] = 0;
    rejectView(resource, 15, "VIEW CEL READ ITS DESCRIPTION TERMINATOR AS A ROW TERMINATOR");
}

#pragma code-name (push, "BANKRAM07")
#pragma rodata-name (push, "BANKRAM07")
static unsigned long viewAuditCycles(void)
{
    byte a = *(volatile byte*)0x9fb8;
    byte b = *(volatile byte*)0x9fb9;
    byte c = *(volatile byte*)0x9fba;
    byte d = *(volatile byte*)0x9fbb;
    return ((unsigned long)d << 24) | ((unsigned long)c << 16) |
        ((unsigned)b << 8) | a;
}

static void multiloopLoadWorkload(void)
{
    enum { LOOPS = 12, CELS = 8, HEADER_SIZE = 5 + LOOPS * 2,
        LOOP_HEADER_SIZE = 1 + CELS * 2, CEL_SIZE = 5,
        RESOURCE_SIZE = HEADER_SIZE + LOOPS * (LOOP_HEADER_SIZE + CELS * CEL_SIZE),
        SAMPLES = 3 };
    byte resource[RESOURCE_SIZE], loop, cel, loaded, sample;
    unsigned int cursor = HEADER_SIZE;
    unsigned long start, elapsed, total = 0, average, worst = 0;
    AGIFilePosType previous;
    char line[112];

    memset(resource, 0, sizeof(resource));
    resource[2] = LOOPS;
    for (loop = 0; loop < LOOPS; ++loop) {
        unsigned int loopStart = cursor;
        resource[5 + loop * 2] = (byte)loopStart;
        resource[6 + loop * 2] = (byte)(loopStart >> 8);
        resource[loopStart] = CELS;
        for (cel = 0; cel < CELS; ++cel) {
            unsigned int cellStart = loopStart + LOOP_HEADER_SIZE + cel * CEL_SIZE;
            unsigned int cellOffset = cellStart - loopStart;
            unsigned int offsetEntry = loopStart + 1 + cel * 2;
            resource[offsetEntry] = (byte)cellOffset;
            resource[offsetEntry + 1] = (byte)(cellOffset >> 8);
            resource[cellStart] = 3;
            resource[cellStart + 1] = 1;
            resource[cellStart + 3] = 0x33;
        }
        cursor += LOOP_HEADER_SIZE + CELS * CEL_SIZE;
    }
    auditCheck(cursor == sizeof(resource), "multiloop VIEW workload size mismatch");
    writeViewVolume(resource, sizeof(resource));
    setViewDirectory(&previous);
    for (sample = 0; sample < SAMPLES; ++sample) {
        start = viewAuditCycles();
        loaded = b9TryLoadViewFile(VIEW_AUDIT_NUMBER);
        elapsed = viewAuditCycles() - start;
        auditCheck(loaded, "valid multiloop VIEW load workload was rejected");
        total += elapsed;
        if (elapsed > worst) worst = elapsed;
        b9DiscardView(VIEW_AUDIT_NUMBER);
    }
    memCpyBanked((byte*)&viewdir[VIEW_AUDIT_NUMBER], (byte*)&previous,
        DIRECTORY_BANK, sizeof(previous));
    auditCheck(remove("vol.8") == 0, "multiloop VIEW workload cleanup");
    average = total / SAMPLES;
    auditCheck(average < VIEW_MULTILOOP_CYCLE_LIMIT && worst < VIEW_MULTILOOP_CYCLE_LIMIT,
        "multiloop VIEW load exceeded half its original cycle baseline");
    sprintf(line, "VIEW MULTILOOP LOAD BASELINE old-average=%lu old-worst=%lu samples=%u loops=%u cels=%u bytes=%u average=%lu worst=%lu cycles\n",
        VIEW_MULTILOOP_BASELINE_AVERAGE, VIEW_MULTILOOP_BASELINE_WORST,
        SAMPLES, LOOPS, LOOPS * CELS, (unsigned int)sizeof(resource), average, worst);
    auditOutput(line);
}
#pragma rodata-name (pop)
#pragma code-name (pop)
#pragma rodata-name (pop)
#pragma code-name (pop)
#pragma code-name (push, "BANKRAM09")
#pragma rodata-name (push, "BANKRAM09")
static void validViewRleCase(void)
{
    AGIFilePosType previous;
    View view;
    Loop loop;
    Cel cel;
    byte loaded, i;
    unsigned long address;

    writeValidViewFixture();
    setViewDirectory(&previous);
    loaded = b9TryLoadViewFile(VIEW_AUDIT_NUMBER);
	auditCheck(loaded, "valid short, transparent, mirrored or dense VIEW rows were rejected");
    getLoadedView(&view, VIEW_AUDIT_NUMBER);
    getLoadedLoop(&view, &loop, 0);
    getLoadedCel(&loop, &cel, 0);
    auditCheck(cel.width == 3 && cel.height == 2 && !cel.flipped, "valid short-row cel metadata changed");

    b8SetupLineTables();
    address = b8GetVeraPictureAddress(10, 9);
    for (i = 0; i < 3; ++i) testPoke(0x44, address + i);
    for (i = 0; i < 3; ++i) testPoke(0x44, address + BYTES_PER_ROW + i);
    for (i = 0; i < 80; ++i) {
        testPoke(0x44, PRIORITY_START + 9UL * 80 + i);
        testPoke(0x44, PRIORITY_START + 10UL * 80 + i);
        testPoke(0x44, PRIORITY_START + 30UL * 80 + i);
    }
    b9AddToPic(VIEW_AUDIT_NUMBER, 0, 0, 10, 10, 15, 15);
    auditCheck(testPeek(address) == 0x11 && testPeek(address + 1) == 0x11 &&
               testPeek(address + 2) == 0x44, "early terminator did not pad transparency");
    auditCheck(testPeek(address + BYTES_PER_ROW) == 0x44 &&
               testPeek(address + BYTES_PER_ROW + 2) == 0x44, "valid fully transparent row changed the picture");

    mirroredViewCase();

    denseViewCase();

    bEResetSpritesUpdatedBuffer(); bEResetViewTableMetadata(); bDResetSpriteMemoryManager();
    b9DiscardView(VIEW_AUDIT_NUMBER);
    memCpyBanked((byte*)&viewdir[VIEW_AUDIT_NUMBER], (byte*)&previous, DIRECTORY_BANK, sizeof(previous));
    auditCheck(remove("vol.8") == 0, "valid VIEW fixture cleanup");
}
#pragma rodata-name (pop)
#pragma code-name (pop)

#pragma code-name (push, "BANKRAM0F")
#pragma rodata-name (push, "BANKRAM0F")
static void viewHeaderCase(byte loops)
{
    View local, stored;
    AGIFile file;
    byte bank, i, guard[16], previousBank = RAM_BANK;
    unsigned index;
    memset(&local, 0, sizeof(local));
    memset(&file, 0, sizeof(file));
    memset(viewHeaderBytes, 0, sizeof(viewHeaderBytes));
    viewHeaderBytes[2] = loops;
    for (index=0; index<loops; ++index) {
        viewHeaderBytes[5+index*2] = (byte)(40+index);
        viewHeaderBytes[6+index*2] = (byte)(index>>4);
    }
    file.code = b10BankedAlloc(sizeof(viewHeaderBytes), &bank);
    file.codeBank = bank; file.codeSize = sizeof(viewHeaderBytes);
    memCpyBankedBetween(file.code, bank, viewHeaderBytes, 0x0f, sizeof(viewHeaderBytes));
    memsetBanked(viewHeaderBuffer, 0xa5, 501, VIEW_CODE_BANK_1);
    memsetBanked(loopHeaderBuffer, 0xe7, 16, VIEW_CODE_BANK_1);
	setViewData(250, &file, &local);
	getLoadedView(&stored, 250);
	auditCheck(stored.loaded == local.loaded, "view loaded metadata mismatch");
	auditCheck(stored.numberOfLoops == local.numberOfLoops, "view loop count metadata mismatch");
	auditCheck(stored.loops == local.loops, "view loop pointer metadata mismatch");
	auditCheck(stored.loopsBank == local.loopsBank, "view loop bank metadata mismatch");
	auditCheck(stored.description == local.description, "view description metadata mismatch");
	auditCheck(local.codeBlock == file.code, "setViewData lost its input resource pointer");
	auditCheck(stored.codeBlock == file.code, "loaded view lost its input resource pointer");
	auditCheck(stored.codeBlockBank == local.codeBlockBank, "view resource bank metadata mismatch");
	auditCheck(stored.maxCels == local.maxCels, "view max cel metadata mismatch");
	auditCheck(stored.maxVeraSlots == local.maxVeraSlots, "view max sprite slot metadata mismatch");
    for (index = 0; index < 5 + (unsigned)loops * 2; ++index) {
        memCpyBanked(guard, viewHeaderBuffer + index, VIEW_CODE_BANK_1, 1);
        auditCheck(guard[0] == viewHeaderBytes[index],
            "view header lost loop offsets after loop seven");
    }
    memCpyBanked(guard, loopHeaderBuffer, VIEW_CODE_BANK_1, sizeof(guard));
    for (i=0; i<sizeof(guard); ++i) auditCheck(guard[i] == 0xe7, "view header overflowed neighboring loop buffer");
    b9DiscardView(250);
    RAM_BANK = previousBank;
}
#pragma rodata-name (pop)
#pragma code-name (pop)
#pragma code-name (push, "BANKRAM07")
#pragma rodata-name (push, "BANKRAM07")
static void loopHeaderCase(void)
{
    byte bank, before[16], after[16], previousBank = RAM_BANK;
    View view;
    Loop loop;
    AGIFile file;
    memset(loopHeaderBytes, 0, sizeof(loopHeaderBytes)); loopHeaderBytes[0]=255;
    memset(&view,0,sizeof(view)); memset(&loop,0,sizeof(loop)); memset(&file,0,sizeof(file));
    view.loaded=TRUE; view.numberOfLoops=1;
    view.loops=(Loop*)b10BankedAlloc(sizeof(Loop), &view.loopsBank);
    memsetBanked(view.loops,0,sizeof(Loop),view.loopsBank);
    file.code=b10BankedAlloc(sizeof(loopHeaderBytes),&bank); file.codeBank=bank;
    memCpyBankedBetween(file.code, bank, loopHeaderBytes, 0x0f, sizeof(loopHeaderBytes));
    memCpyBanked(before,(byte*)b9CelToVera,VIEW_CODE_BANK_1,sizeof(before));
    setLoopData(&file,&view,&loop,file.code,250,0);
    memCpyBanked(after,(byte*)b9CelToVera,VIEW_CODE_BANK_1,sizeof(after));
    auditCheck(!memcmp(before,after,sizeof(before)),"loop header overwrote neighboring sprite-transfer code");
    auditCheck(loop.numberOfCels==255,"loop header lost maximum cel count");
    auditCheck(b10BankedDealloc((byte*)loop.cels,loop.celsBank),"loop cel allocation cleanup");
    auditCheck(b10BankedDealloc((byte*)view.loops,view.loopsBank),"loop allocation cleanup");
    auditCheck(b10BankedDealloc(file.code,bank),"loop resource allocation cleanup");
    RAM_BANK = previousBank;
}
#pragma rodata-name (pop)
#pragma code-name (pop)

#pragma code-name (push, "BANKRAM02")
#pragma rodata-name (push, "BANKRAM02")
static void viewsAuditCasesBanked(void)
{
    auditOutput("VIEW RLE AND HEADER CASES; ");
    viewRleCases();
    viewHeaderCase(8);
    viewHeaderCase(14);
    viewHeaderCase(255);
    loopHeaderCase();
    bankEdgeViewCase(FALSE);
    bankEdgeViewCase(TRUE);
    validViewRleCase();
}
#pragma rodata-name (pop)
#pragma code-name (pop)
#pragma rodata-name (pop)
#pragma code-name (pop)
#pragma bss-name (pop)
void viewsAuditCases(void)
{
    viewsAuditCasesBanked();
}
