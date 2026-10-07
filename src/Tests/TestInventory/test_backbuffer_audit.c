#include "audit.h"
#include "../../view.h"
#include <string.h>

extern boolean agiBlit(byte entry, boolean disableInterrupts);
extern void setLoadedLoop(View* view, Loop* loop, byte number);
extern void setLoadedCel(Loop* loop, Cel* cel, byte number);
extern byte bDSpriteAllocTable[];
extern byte allocationArray[];
extern byte bESpritesUpdatedBuffer[];
extern byte* bESpritesUpdatedBufferPointer;
extern byte mainLoopCounter;
extern volatile unsigned int testVsyncIrqs;
extern volatile byte bERetirementCount, bERetirementReadyCount;
extern byte testStatus(void);
extern void testDisableInterrupts(void), testEnableInterrupts(void);

/* This table crosses into bank-switching production helpers. */
static ViewTable fixtureTable;

#pragma wrapped-call (push, trampoline, SPRITE_METADATA_BANK)
extern void bETerminateSpriteBuffer(void);
extern void bECollectRetiredBackBuffers(void);
#pragma wrapped-call (pop)

#pragma wrapped-call (push, trampoline, SPRITE_MEMORY_MANAGER_BANK)
extern void bDResetSpriteTablePointer(void);
#pragma wrapped-call (pop)
#pragma wrapped-call (push, trampoline, 0x08)
void makeResizeFixture(void);
#pragma wrapped-call (pop)
#pragma wrapped-call (push, trampoline, 0x07)
void splitBackBufferRetiresEverySlot(void);
#pragma wrapped-call (pop)
#pragma wrapped-call (push, trampoline, 0x0F)
void roomResetCancelsPendingPublication(void);
void delayedPublicationRetirement(void);
#pragma wrapped-call (pop)
#pragma wrapped-call (push, trampoline, 0x0D)
void verifyFailedBackBufferAllocation(VeraSpriteAddress old0,
    VeraSpriteAddress old1, VeraSpriteAddress oneFreeSpan);
#pragma wrapped-call (pop)
#pragma wrapped-call (push, trampoline, 0x0E)
void configureSplitResizeFixture(void);
#pragma wrapped-call (pop)
#pragma wrapped-call (push, trampoline, 0x0A)
void configureSplitOldBufferFixture(void);
#pragma wrapped-call (pop)
#pragma wrapped-call (push, trampoline, 0x0C)
void unpublishedResizeIsRejected(void);
#pragma wrapped-call (pop)
#pragma wrapped-call (push, trampoline, 0x0B)
void replacementAllocationFailureRollsBack(void);
#pragma wrapped-call (pop)
#pragma wrapped-call (push, trampoline, 0x02)
void retryAfterBackBufferAllocationFailure(VeraSpriteAddress old0,
    VeraSpriteAddress old1, VeraSpriteAddress oneFreeSpan);
#pragma wrapped-call (pop)
#pragma wrapped-call (push, trampoline, 0x02)
void collectAndCleanupRetriedBackBuffer(VeraSpriteAddress old0,
    VeraSpriteAddress old1);
#pragma wrapped-call (pop)
extern VeraSpriteAddress bDFindFreeVramBlock(SpriteAllocationSize width, SpriteAllocationSize height);
extern void bDDeleteAllocation(VeraSpriteAddress address, SpriteAllocationSize width, SpriteAllocationSize height);

#pragma wrapped-call (push, trampoline, VIEW_CODE_BANK_1)
extern void b9AnimateObjects(void);
#pragma wrapped-call (pop)
#pragma wrapped-call (push, trampoline, 0x0C)
void checkPublishedBackBufferForEntry(byte entry, byte segments);
#pragma wrapped-call (pop)
unsigned long steadyBackBufferBlitWorkload(void);
void compareRamToWarmCache(void);
#pragma wrapped-call (push, trampoline, CONTROLLER_BANK)
byte copyBackBufferAddresses(byte entry, VeraSpriteAddress* addresses);
#pragma wrapped-call (pop)
#pragma wrapped-call (push, trampoline, 0x08)
void checkVramSpan(VeraSpriteAddress address, byte width, byte height, byte allocated);
#pragma wrapped-call (pop)

#pragma wrapped-call (push, trampoline, CONTROLLER_BANK)
void publishUpdates(void);
void queueBlitUpdate(byte entry, byte loopNumber, byte animated, byte expectEmptyBuffer);
void blitAndPublish(byte entry, byte loopNumber, byte animated);
unsigned long activeBackBufferAddress(byte entry);
void captureVramBaseline(void);
void compareRamToBaseline(void);
void compareVramToBaseline(void);
#pragma wrapped-call (pop)

#pragma wrapped-call (push, trampoline, 0x08)
void checkVramBlock(VeraSpriteAddress address, byte allocated, const char* message);
#pragma wrapped-call (pop)

#pragma wrapped-call (push, trampoline, VIEW_CODE_BANK_2)
extern void bAResetViews(void);
#pragma wrapped-call (pop)

#pragma code-name (push, "BANKRAM01")
#pragma rodata-name (push, "BANKRAM01")
#pragma bss-name (push, "BANKRAM01")
static byte baselineRam[ALLOCATION_ARRAY_SIZE];
static byte warmRam[ALLOCATION_ARRAY_SIZE];
static byte current[64];
static unsigned baselineVramUsed;
#pragma bss-name (pop)

void publishUpdates(void)
{
    unsigned int before = testVsyncIrqs;
    unsigned int budget = 0xffffu;
    ++mainLoopCounter;
    testEnableInterrupts();
    while (budget && testVsyncIrqs == before) --budget;
    auditCheck(testVsyncIrqs != before, "VSYNC timeout");
    testDisableInterrupts();
}

void queueBlitUpdate(byte entry, byte loopNumber, byte animated, byte expectEmptyBuffer)
{
    View loaded;
    Loop loop;
    Cel cel;
    byte* updatePointer;
    byte previousBank, previousIrq;
    getViewTab(&fixtureTable, entry);
    fixtureTable.currentLoop = loopNumber;
    fixtureTable.staleCounter = animated;
    setViewTab(&fixtureTable, entry);
    getViewTab(&fixtureTable, entry);
    auditCheck(fixtureTable.currentLoop == loopNumber, "loop not set");
    getLoadedView(&loaded, 244);
    auditCheck(loaded.loaded, "view unloaded");
    auditCheck(loaded.numberOfLoops == 2, "loop count");
    auditCheck(loaded.loops != NULL, "loop pointer");
    getLoadedLoop(&loaded, &loop, loopNumber);
    auditCheck(loop.cels && loop.numberOfCels == 1,
        "loop or cel");
    getLoadedCel(&loop, &cel, 0);
    auditCheck(cel.bmp != NULL, "cel bitmap");
    auditCheck(loop.allocationWidth == (cel.width > SPR_SIZE_16 ? SPR_ATTR_64 : SPR_ATTR_8), "loop size");
    memCpyBanked((byte*)&updatePointer, (byte*)&bESpritesUpdatedBufferPointer, SPRITE_UPDATED_BANK, sizeof(updatePointer));
    if (expectEmptyBuffer)
        auditCheck(updatePointer == bESpritesUpdatedBuffer, "buffer busy");
    else
        auditCheck(updatePointer != bESpritesUpdatedBuffer, "batch empty");
    previousBank = RAM_BANK;
    previousIrq = testStatus() & 4;
    auditCheck(agiBlit(entry, FALSE), "blit rejected");
    auditCheck(RAM_BANK == previousBank && (testStatus() & 4) == previousIrq,
        "caller state");
}

void blitAndPublish(byte entry, byte loopNumber, byte animated)
{
    queueBlitUpdate(entry, loopNumber, animated, TRUE);
    bETerminateSpriteBuffer();
    publishUpdates();
}

unsigned long activeBackBufferAddress(byte entry)
{
    ViewTableMetadata metadata;
    unsigned long address;
    memCpyBanked((byte*)&metadata, (byte*)&viewTableMetadata[entry], SPRITE_METADATA_BANK, sizeof(metadata));
    memCpyBankedBetween((byte*)&address, CONTROLLER_BANK, (byte*)metadata.backBuffers,
        metadata.viewTableMetadataBank, sizeof(address));
    return address;
}

byte copyBackBufferAddresses(byte entry, VeraSpriteAddress* addresses)
{
    ViewTableMetadata metadata;
    memCpyBanked((byte*)&metadata, (byte*)&viewTableMetadata[entry], SPRITE_METADATA_BANK, sizeof(metadata));
    memCpyBanked((byte*)addresses, (byte*)metadata.backBuffers,
        metadata.viewTableMetadataBank, metadata.backBufferSlots * sizeof(VeraSpriteAddress));
    return metadata.backBufferSlots;
}

#pragma code-name (push, "BANKRAM08")
#pragma rodata-name (push, "BANKRAM08")
void checkVramBlock(VeraSpriteAddress address, byte allocated, const char* message)
{
    byte state;
    unsigned int block = (address - VRAM_START) / BLOCK_SIZE;
    memCpyBanked((byte*)&state, bDSpriteAllocTable + block, SPRITE_MEMORY_MANAGER_BANK, sizeof(state));
    auditCheck((state != 0) == allocated, message);
}

void checkVramSpan(VeraSpriteAddress address, byte width, byte height, byte allocated)
{
    unsigned int block = (address - VRAM_START) / BLOCK_SIZE;
    unsigned int blocks = ((unsigned int)width * height) / (BLOCK_SIZE * 2);
    byte state;
    while (blocks--)
    {
        memCpyBanked((byte*)&state, bDSpriteAllocTable + block++, SPRITE_MEMORY_MANAGER_BANK, sizeof(state));
        auditCheck((state != 0) == allocated, allocated ? "allocation span freed" : "allocation span still owned");
    }
}
#pragma rodata-name (pop)
#pragma code-name (pop)

#pragma code-name (push, "BANKRAM0C")
#pragma rodata-name (push, "RODATA")
void checkPublishedBackBufferForEntry(byte entry, byte segments)
{
    ViewTableMetadata metadata;
    unsigned long attrAddress;
    VeraSpriteAddress addresses[MAX_SPRITES_SLOTS_PER_VIEW_TAB];
    byte segment, slot, found;
    memCpyBanked((byte*)&metadata, (byte*)&viewTableMetadata[entry], SPRITE_METADATA_BANK, sizeof(metadata));
    memCpyBanked((byte*)addresses, (byte*)metadata.backBuffers,
        metadata.viewTableMetadataBank, metadata.backBufferSlots * sizeof(VeraSpriteAddress));
    auditCheck(metadata.isOnBackBuffer && addresses[0] != 0, "no backbuffer");
    auditCheck(segments <= metadata.backBufferSlots, "bad segment count");
    for (segment = 0; segment < segments; ++segment) {
        found = FALSE;
        for (slot = 1; slot < SPRITE_SLOTS; ++slot) {
            attrAddress = SPRITE_ATTRIBUTES_START + (unsigned long)slot * 8UL;
            if (testPeek(attrAddress) == (byte)(addresses[segment] >> 5) &&
                testPeek(attrAddress + 1UL) == (byte)(addresses[segment] >> 13)) found = TRUE;
        }
        if (!found) auditCheck(FALSE, entry ? (segment ? "E1S1 missing" : "E1S0 missing") :
            (segment ? "E0S1 missing" : "E0S0 missing"));
        auditCheck(testPeek(addresses[segment]) == 0x11, "bad pixel");
    }
}
#pragma rodata-name (pop)
#pragma code-name (pop)

#pragma rodata-name (push, "RODATA")
void compareVramToBaseline(void)
{
    unsigned offset;
    byte length;
    unsigned usedCurrent = 0;
    for (offset = 0; offset < TOTAL_REAL_BLOCKS; offset += sizeof(current)) {
        length = TOTAL_REAL_BLOCKS - offset > sizeof(current) ? sizeof(current) : TOTAL_REAL_BLOCKS - offset;
        memCpyBankedBetween(current, CONTROLLER_BANK, bDSpriteAllocTable + offset,
            SPRITE_MEMORY_MANAGER_BANK, length);
        while (length) {
            if (current[length - 1]) ++usedCurrent;
            --length;
        }
    }
    auditCheck(usedCurrent == baselineVramUsed, "VRAM leak");
}

void captureVramBaseline(void)
{
    unsigned offset;
    byte length, i;
    baselineVramUsed = 0;
    for (offset = 0; offset < TOTAL_REAL_BLOCKS; offset += sizeof(current)) {
        length = TOTAL_REAL_BLOCKS - offset > sizeof(current) ? sizeof(current) : TOTAL_REAL_BLOCKS - offset;
        memCpyBankedBetween(current, CONTROLLER_BANK, bDSpriteAllocTable + offset,
            SPRITE_MEMORY_MANAGER_BANK, length);
        for (i = 0; i < length; ++i)
            if (current[i]) ++baselineVramUsed;
    }
}

void compareRamToBaseline(void)
{
    unsigned offset;
    byte length;
    for (offset = 0; offset < ALLOCATION_ARRAY_SIZE; offset += sizeof(current)) {
        length = ALLOCATION_ARRAY_SIZE - offset > sizeof(current) ? sizeof(current) : ALLOCATION_ARRAY_SIZE - offset;
        memCpyBankedBetween(current, CONTROLLER_BANK, allocationArray + offset,
            MEMORY_MANAGEMENT_BANK, length);
        auditCheck(!memcmp(current, baselineRam + offset, length), "RAM leak");
    }
}

void compareRamToWarmCache(void)
{
	unsigned offset;
	byte length;
	for (offset = 0; offset < ALLOCATION_ARRAY_SIZE; offset += sizeof(current))
	{
		length = ALLOCATION_ARRAY_SIZE - offset > sizeof(current) ? sizeof(current) : ALLOCATION_ARRAY_SIZE - offset;
		memCpyBankedBetween(current, CONTROLLER_BANK, allocationArray + offset,
			MEMORY_MANAGEMENT_BANK, length);
		auditCheck(!memcmp(current, warmRam + offset, length), "warmcache RAM changed");
	}
}
#pragma rodata-name (pop)

static unsigned long backbufferCycles(void)
{
	byte a = *(volatile byte*)0x9fb8;
	byte b = *(volatile byte*)0x9fb9;
	byte c = *(volatile byte*)0x9fba;
	byte d = *(volatile byte*)0x9fbb;
	return ((unsigned long)d << 24) | ((unsigned long)c << 16) | ((unsigned)b << 8) | a;
}

/* The pre-ready-prefix 8-blit mean was 25,989 cycles; cap at 30,000. */
#define BACKBUFFER_BLIT_CYCLE_LIMIT 30000UL

unsigned long steadyBackBufferBlitWorkload(void)
{
	unsigned long start, elapsed, value;
	char digits[11];
	byte i, count = 0;
	start = backbufferCycles();
	for (i = 0; i < 8; ++i) auditCheck(agiBlit(0, FALSE), "steady blit rejected");
	elapsed = (backbufferCycles() - start) / 8;
	auditCheck(elapsed <= BACKBUFFER_BLIT_CYCLE_LIMIT, "steady blit cycle ceiling");
	bETerminateSpriteBuffer();
	auditOutput("BACKBUFFER STEADY BLIT CYCLES: ");
	value = elapsed;
	do { digits[count++] = '0' + value % 10; value /= 10; } while (value);
	while (count) *(volatile byte*)0x9fbb = digits[--count];
	auditOutput("; ");
	publishUpdates();
	return elapsed;
}

#pragma code-name (push, "BANKRAM0F")
#pragma rodata-name (push, "RODATA")
void delayedPublicationRetirement(void)
{
	unsigned long oldAddresses[2], newAddresses[2];
    unsigned long attrAddresses[2];
    VeraSpriteAddress reusable;
    byte oldLow[2], oldHigh[2], remaining, i;
    bAResetViews();
    memCpyBankedBetween(baselineRam, CONTROLLER_BANK, allocationArray,
        MEMORY_MANAGEMENT_BANK, sizeof(baselineRam));
    captureVramBaseline();
    makeResizeFixture();
    getViewTab(&fixtureTable, 0);
    setViewTab(&fixtureTable, 1);
    testDisableInterrupts();
    for (i = 0; i < 2; ++i) queueBlitUpdate(i, 0, FALSE, !i);
    bETerminateSpriteBuffer();
    publishUpdates();
    for (i = 0; i < 2; ++i) queueBlitUpdate(i, 0, TRUE, !i);
    bETerminateSpriteBuffer();
    publishUpdates();
    for (i = 0; i < 2; ++i) {
        oldAddresses[i] = activeBackBufferAddress(i);
        attrAddresses[i] = SPRITE_ATTRIBUTES_START + ((unsigned long)i + 1UL) * 8UL;
        oldLow[i] = testPeek(attrAddresses[i]);
        oldHigh[i] = testPeek(attrAddresses[i] + 1UL);
	}
	auditCheck(oldAddresses[0] != oldAddresses[1], "buffers aliased");

	for (i = 0; i < 2; ++i) {
		queueBlitUpdate(i, 1, TRUE, !i);
        newAddresses[i] = activeBackBufferAddress(i);
        if (newAddresses[i] == oldAddresses[i]) {
            ViewTableMetadata metadata;
            memCpyBanked((byte*)&metadata, (byte*)&viewTableMetadata[i], SPRITE_METADATA_BANK, sizeof(metadata));
            auditCheck(FALSE, metadata.backBufferWidth == SPR_SIZE_64 ? "entry 1 buffer reused" : "entry 1 not resized");
        }
        auditCheck(testPeek(attrAddresses[i]) == oldLow[i] && testPeek(attrAddresses[i] + 1UL) == oldHigh[i],
            "attrs pre-IRQ");
        auditCheck(testPeek(oldAddresses[i]) == 0x11, "pixel pre-IRQ");
        checkVramBlock(oldAddresses[i], TRUE, "freed pre-IRQ");
    }
	bETerminateSpriteBuffer();
	remaining = bERetirementCount;
	auditCheck(remaining == 2 && remaining <= MAX_BACKBUFFER_RETIREMENTS &&
		!bERetirementReadyCount, "retire count before IRQ");
    bECollectRetiredBackBuffers();
    for (i = 0; i < 2; ++i)
        checkVramBlock(oldAddresses[i], TRUE, "freed pre-IRQ");

	publishUpdates();
	auditCheck(bERetirementReadyCount == 2, "IRQ did not ready retirements");
    for (i = 0; i < 2; ++i) {
        checkPublishedBackBufferForEntry(i, 1);
		checkVramBlock(oldAddresses[i], TRUE, "IRQ freed old");
	}

	queueBlitUpdate(0, 1, TRUE, TRUE);
	bETerminateSpriteBuffer();
	publishUpdates();
	auditCheck(bERetirementReadyCount == 2, "later publication lost ready prefix");
	for (i = 0; i < 2; ++i) checkVramBlock(oldAddresses[i], TRUE, "retired before normal update");

	testEnableInterrupts();
	b9AnimateObjects();
	auditCheck(!(testStatus() & 4), "collector IRQ");
	testDisableInterrupts();
	auditCheck(!bERetirementReadyCount, "collector left ready retirement");
    for (i = 0; i < 2; ++i)
        checkVramBlock(oldAddresses[i], FALSE, "old buffer owned");
	remaining = bERetirementCount;
	auditCheck(!remaining, "queue not empty");

	testEnableInterrupts();
	b9AnimateObjects();
	testDisableInterrupts();
	publishUpdates();
	auditCheck(!bERetirementReadyCount && !bERetirementCount,
		"empty update changed retirement readiness");

	bDResetSpriteTablePointer();
    reusable = bDFindFreeVramBlock(SPR_SIZE_8, SPR_SIZE_8);
    auditCheck(reusable != 0, "alloc failed");
	bDDeleteAllocation(reusable, SPR_SIZE_8, SPR_SIZE_8);

    bAResetViews();
    testEnableInterrupts();
    b9DiscardView(244);
    compareRamToBaseline();
	compareVramToBaseline();
}
#pragma rodata-name (pop)
#pragma code-name (pop)

#pragma code-name (push, "BANKRAM07")
#pragma rodata-name (push, "RODATA")
void splitBackBufferRetiresEverySlot(void)
{
    VeraSpriteAddress oldAddresses[MAX_SPRITES_SLOTS_PER_VIEW_TAB];
    VeraSpriteAddress newAddresses[MAX_SPRITES_SLOTS_PER_VIEW_TAB];
    VeraSpriteAddress neighbor;
    ViewTableMetadata metadata;
    byte oldAttrLow[2], oldAttrHigh[2];
    byte oldSlots, newSlots, i, remaining;
    bAResetViews();
    memCpyBankedBetween(baselineRam, CONTROLLER_BANK, allocationArray,
        MEMORY_MANAGEMENT_BANK, sizeof(baselineRam));
    captureVramBaseline();
    makeResizeFixture();
    configureSplitOldBufferFixture();
    testDisableInterrupts();
    blitAndPublish(0, 0, FALSE);
    blitAndPublish(0, 0, TRUE);

    oldSlots = copyBackBufferAddresses(0, oldAddresses);
    auditCheck(oldSlots == 2 && oldAddresses[0] != oldAddresses[1], "split slots missing");
    memCpyBanked((byte*)&metadata, (byte*)&viewTableMetadata[0],
        SPRITE_METADATA_BANK, sizeof(metadata));
    auditCheck(metadata.backBufferWidth == SPR_SIZE_64 &&
        metadata.backBufferHeight == SPR_SIZE_8 && metadata.backBufferSlots == 2,
        "old split geometry");
    checkPublishedBackBufferForEntry(0, 2);
    for (i = 0; i < oldSlots; ++i) {
        checkVramSpan(oldAddresses[i], SPR_SIZE_64, SPR_SIZE_8, TRUE);
        oldAttrLow[i] = testPeek(SPRITE_ATTRIBUTES_START + (unsigned long)(i + 1) * 8UL);
        oldAttrHigh[i] = testPeek(SPRITE_ATTRIBUTES_START + (unsigned long)(i + 1) * 8UL + 1UL);
    }
    neighbor = bDFindFreeVramBlock(SPR_SIZE_8, SPR_SIZE_8);
    auditCheck(neighbor != 0, "neighbor failed");
    testPoke(0x5a, neighbor);
    checkVramSpan(neighbor, SPR_SIZE_8, SPR_SIZE_8, TRUE);

    queueBlitUpdate(0, 1, TRUE, TRUE);
    newSlots = copyBackBufferAddresses(0, newAddresses);
    auditCheck(newSlots == 2 && newAddresses[0] != oldAddresses[0] &&
        newAddresses[1] != oldAddresses[1], "split slots unchanged");
    memCpyBanked((byte*)&metadata, (byte*)&viewTableMetadata[0],
        SPRITE_METADATA_BANK, sizeof(metadata));
    auditCheck(metadata.backBufferWidth == SPR_SIZE_64 &&
        metadata.backBufferHeight == SPR_SIZE_16 && metadata.backBufferSlots == 2,
        "replacement split geometry");
    bETerminateSpriteBuffer();
    for (i = 0; i < oldSlots; ++i) {
        checkVramSpan(oldAddresses[i], SPR_SIZE_64, SPR_SIZE_8, TRUE);
        auditCheck(testPeek(SPRITE_ATTRIBUTES_START + (unsigned long)(i + 1) * 8UL) == oldAttrLow[i] &&
            testPeek(SPRITE_ATTRIBUTES_START + (unsigned long)(i + 1) * 8UL + 1UL) == oldAttrHigh[i],
            "split attrs changed pre-IRQ");
        auditCheck(testPeek(neighbor) == 0x5a, "split neighbor changed");
    }
    remaining = bERetirementCount;
    auditCheck(remaining == 1, "split retirement");
    publishUpdates();
    auditCheck(bERetirementReadyCount == 1, "split IRQ did not ready retirement");
    checkPublishedBackBufferForEntry(0, 2);
    for (i = 0; i < oldSlots; ++i)
        checkVramSpan(oldAddresses[i], SPR_SIZE_64, SPR_SIZE_8, TRUE);

    testEnableInterrupts();
    b9AnimateObjects();
    testDisableInterrupts();
    for (i = 0; i < oldSlots; ++i)
        checkVramSpan(oldAddresses[i], SPR_SIZE_64, SPR_SIZE_8, FALSE);
    auditCheck(testPeek(neighbor) == 0x5a, "neighbor changed");
    checkVramSpan(neighbor, SPR_SIZE_8, SPR_SIZE_8, TRUE);

    bAResetViews();
    testEnableInterrupts();
    b9DiscardView(244);
    compareRamToBaseline();
    compareVramToBaseline();
}
#pragma rodata-name (pop)
#pragma code-name (pop)

#pragma code-name (push, "BANKRAM0C")
#pragma rodata-name (push, "RODATA")
void unpublishedResizeIsRejected(void)
{
	ViewTableMetadata metadata;
	VeraSpriteAddress oldAddress, newAddress;
	byte* updatePointer;
	byte previousBank, previousIrq, pending;
	unsigned long attrAddress = SPRITE_ATTRIBUTES_START + 8UL;
	byte oldLow, oldHigh;
    bAResetViews();
	memCpyBankedBetween(baselineRam, CONTROLLER_BANK, allocationArray,
		MEMORY_MANAGEMENT_BANK, sizeof(baselineRam));
	captureVramBaseline();
	makeResizeFixture();
	testDisableInterrupts();
	blitAndPublish(0, 0, FALSE);
	blitAndPublish(0, 1, FALSE);
	blitAndPublish(0, 0, TRUE);
	oldAddress = activeBackBufferAddress(0);
	oldLow = testPeek(attrAddress);
	oldHigh = testPeek(attrAddress + 1UL);

	queueBlitUpdate(0, 1, TRUE, TRUE);
	newAddress = activeBackBufferAddress(0);
	memCpyBanked((byte*)&updatePointer, (byte*)&bESpritesUpdatedBufferPointer,
		SPRITE_UPDATED_BANK, sizeof(updatePointer));
	pending = bERetirementCount;
	auditCheck(newAddress != oldAddress && pending == 1 &&
		updatePointer != bESpritesUpdatedBuffer, "resize did not stage pending owner");
	checkVramBlock(oldAddress, TRUE, "old freed before publication");
	auditCheck(testPeek(attrAddress) == oldLow && testPeek(attrAddress + 1UL) == oldHigh,
		"old attributes changed before publication");

	getViewTab(&fixtureTable, 0);
	fixtureTable.currentLoop = 0;
	fixtureTable.staleCounter = TRUE;
	setViewTab(&fixtureTable, 0);
	previousBank = RAM_BANK;
	previousIrq = testStatus() & 4;
	auditCheck(!agiBlit(0, FALSE) && RAM_BANK == previousBank &&
		(testStatus() & 4) == previousIrq, "unpublished owner resize was not rejected");
	memCpyBanked((byte*)&metadata, (byte*)&viewTableMetadata[0],
		SPRITE_METADATA_BANK, sizeof(metadata));
	auditCheck(metadata.backBufferWidth == SPR_SIZE_64 &&
		metadata.backBufferHeight == SPR_SIZE_64 &&
		activeBackBufferAddress(0) == newAddress, "rejected resize changed ownership");
	memCpyBanked((byte*)&updatePointer, (byte*)&bESpritesUpdatedBufferPointer,
		SPRITE_UPDATED_BANK, sizeof(updatePointer));
	auditCheck(updatePointer != bESpritesUpdatedBuffer && bERetirementCount == 1,
		"rejected resize changed pending update");
	checkVramBlock(oldAddress, TRUE, "old freed before ack");
	checkVramBlock(newAddress, TRUE, "pending buffer unowned");
	auditCheck(testPeek(attrAddress) == oldLow && testPeek(attrAddress + 1UL) == oldHigh,
		"rejected resize changed displayed address");

	bETerminateSpriteBuffer();
	publishUpdates();
	auditCheck(bERetirementReadyCount == 1, "IRQ did not ready rejected resize");
	checkPublishedBackBufferForEntry(0, 1);
	checkVramBlock(oldAddress, TRUE, "IRQ freed old buffer");
	testEnableInterrupts();
	b9AnimateObjects();
	testDisableInterrupts();
	checkVramBlock(oldAddress, FALSE, "normal update did not retire old buffer");
	auditCheck(!bERetirementCount, "normal update left retirement queued");

	bAResetViews();
	testEnableInterrupts();
    b9DiscardView(244);
    compareRamToBaseline();
    compareVramToBaseline();
}
#pragma rodata-name (pop)
#pragma code-name (pop)

#pragma code-name (push, "BANKRAM0B")
#pragma rodata-name (push, "BANKRAM0B")
void replacementAllocationFailureRollsBack(void)
{
    ViewTableMetadata metadata;
    VeraSpriteAddress oldAddresses[2];
    VeraSpriteAddress smallSpanToRelease;
    byte previousBank, previousIrq, i;
    unsigned int freeBlockOffset;
    bAResetViews();
    memCpyBankedBetween(baselineRam, CONTROLLER_BANK, allocationArray,
        MEMORY_MANAGEMENT_BANK, sizeof(baselineRam));
    captureVramBaseline();
    makeResizeFixture();
	configureSplitResizeFixture();
    testDisableInterrupts();
    blitAndPublish(0, 0, FALSE);
    blitAndPublish(0, 1, FALSE);
    blitAndPublish(0, 0, TRUE);
    memCpyBanked((byte*)&metadata, (byte*)&viewTableMetadata[0],
        SPRITE_METADATA_BANK, sizeof(metadata));
    auditCheck(metadata.backBufferSlots == 2, "rollback old slots");
    memCpyBanked((byte*)oldAddresses, (byte*)metadata.backBuffers,
        metadata.viewTableMetadataBank, sizeof(oldAddresses));

    freeBlockOffset = 1024;
    smallSpanToRelease = VRAM_START + (unsigned long)freeBlockOffset * BLOCK_SIZE;
    testEnableInterrupts();
    bDResetSpriteTablePointer();
    /* Reach the allocator's scan threshold before testing a fragmented table. */
    for (i = 0; i < 33; ++i)
        auditCheck(bDFindFreeVramBlock(SPR_SIZE_64, SPR_SIZE_64) != 0,
            "allocator priming");
    memsetBanked(bDSpriteAllocTable, 1, TOTAL_REAL_BLOCKS, SPRITE_MEMORY_MANAGER_BANK);
    memsetBanked(bDSpriteAllocTable + freeBlockOffset, 0, 8, SPRITE_MEMORY_MANAGER_BANK);
    bDResetSpriteTablePointer();
    testDisableInterrupts();

    getViewTab(&fixtureTable, 0);
    fixtureTable.currentLoop = 1;
    fixtureTable.staleCounter = TRUE;
    setViewTab(&fixtureTable, 0);
    previousBank = RAM_BANK;
    previousIrq = testStatus() & 4;
    auditCheck(!agiBlit(0, FALSE) && RAM_BANK == previousBank &&
        (testStatus() & 4) == previousIrq, "resize rollback");
    verifyFailedBackBufferAllocation(oldAddresses[0], oldAddresses[1],
        smallSpanToRelease);
    retryAfterBackBufferAllocationFailure(oldAddresses[0], oldAddresses[1],
        smallSpanToRelease);
}

#pragma rodata-name (pop)
#pragma code-name (pop)
#pragma code-name (push, "BANKRAM02")
#pragma rodata-name (push, "RODATA")
void retryAfterBackBufferAllocationFailure(VeraSpriteAddress old0,
    VeraSpriteAddress old1, VeraSpriteAddress oneFreeSpan)
{
    ViewTableMetadata metadata;
    VeraSpriteAddress newAddresses[2];
    byte previousIrq;
    unsigned int blockOffset;
    unsigned long attrAddress = SPRITE_ATTRIBUTES_START + 8UL;

    blockOffset = (oneFreeSpan - VRAM_START) / BLOCK_SIZE;
    memsetBanked(bDSpriteAllocTable + blockOffset + 8, 0, 8,
        SPRITE_MEMORY_MANAGER_BANK);
    bDResetSpriteTablePointer();
    previousIrq = testStatus() & 4;
    queueBlitUpdate(0, 1, TRUE, TRUE);
    auditCheck((testStatus() & 4) == previousIrq, "resize retry IRQ");
    memCpyBanked((byte*)&metadata, (byte*)&viewTableMetadata[0],
        SPRITE_METADATA_BANK, sizeof(metadata));
    memCpyBanked((byte*)newAddresses, (byte*)metadata.backBuffers,
        metadata.viewTableMetadataBank, sizeof(newAddresses));
    auditCheck(metadata.backBufferSlots == 2 && newAddresses[0] != old0 &&
        newAddresses[0] != old1 && newAddresses[1] != old0 &&
        newAddresses[1] != old1, "resize retry");
    bETerminateSpriteBuffer();
    checkVramBlock(old0, TRUE, "resize retry");
    checkVramBlock(old1, TRUE, "resize retry");
    publishUpdates();
    auditCheck(bERetirementReadyCount == 1, "resize retry IRQ");
    auditCheck(testPeek(attrAddress) == (byte)(newAddresses[0] >> 5) &&
        testPeek(attrAddress + 1UL) == (byte)(newAddresses[0] >> 13) &&
        testPeek(attrAddress + 8UL) == (byte)(newAddresses[1] >> 5) &&
        testPeek(attrAddress + 9UL) == (byte)(newAddresses[1] >> 13) &&
        testPeek(newAddresses[0]) == 0x11 && testPeek(newAddresses[1]) == 0x11,
        "resize retry pixels");
    checkVramBlock(old0, TRUE, "resize retry IRQ");
    checkVramBlock(old1, TRUE, "resize retry IRQ");
    collectAndCleanupRetriedBackBuffer(old0, old1);
}

void collectAndCleanupRetriedBackBuffer(VeraSpriteAddress old0,
    VeraSpriteAddress old1)
{
    testEnableInterrupts();
    b9AnimateObjects();
    testDisableInterrupts();
    checkVramBlock(old0, FALSE, "resize retired");
    checkVramBlock(old1, FALSE, "resize retired");
    bAResetViews();
    testEnableInterrupts();
    b9DiscardView(244);
    compareRamToBaseline();
    compareVramToBaseline();
}
#pragma rodata-name (pop)
#pragma code-name (pop)

#pragma code-name (push, "BANKRAM0D")
#pragma rodata-name (push, "RODATA")
void verifyFailedBackBufferAllocation(VeraSpriteAddress old0,
    VeraSpriteAddress old1, VeraSpriteAddress oneFreeSpan)
{
    ViewTableMetadata metadata;
    VeraSpriteAddress addresses[2];
    byte* updatePointer;
    byte pending;
    unsigned long attrAddress = SPRITE_ATTRIBUTES_START + 8UL;

    memCpyBanked((byte*)&metadata, (byte*)&viewTableMetadata[0],
        SPRITE_METADATA_BANK, sizeof(metadata));
    memCpyBanked((byte*)addresses, (byte*)metadata.backBuffers,
        metadata.viewTableMetadataBank, sizeof(addresses));
    memCpyBanked((byte*)&updatePointer, (byte*)&bESpritesUpdatedBufferPointer,
        SPRITE_UPDATED_BANK, sizeof(updatePointer));
    pending = bERetirementCount;
    auditCheck(metadata.isOnBackBuffer && metadata.backBufferWidth == SPR_SIZE_8 &&
        metadata.backBufferHeight == SPR_SIZE_8 && addresses[0] == old0 &&
        addresses[1] == old1 && updatePointer == bESpritesUpdatedBuffer &&
        !pending && testPeek(attrAddress) == (byte)(old0 >> 5) &&
        testPeek(attrAddress + 1UL) == (byte)(old0 >> 13) &&
        testPeek(old0) == 0x11, "resize rollback");
    checkVramBlock(old0, TRUE, "resize rollback");
    checkVramBlock(old1, TRUE, "resize rollback");
    checkVramBlock(oneFreeSpan, FALSE, "resize rollback");
}
#pragma rodata-name (pop)
#pragma code-name (pop)

#pragma code-name (push, "BANKRAM0F")
#pragma rodata-name (push, "RODATA")
void roomResetCancelsPendingPublication(void)
{
    unsigned long oldAddress;
    unsigned long mouseAttributes = SPRITE_ATTRIBUTES_START;
    unsigned long inspectAttributes = SPRITE_ATTRIBUTES_START + 127UL * 8UL;
    unsigned long firstSpriteZOrder = SPRITE_ATTRIBUTES_START + 8UL + 6UL;
    unsigned long laterSpriteZOrder = SPRITE_ATTRIBUTES_START + 5UL * 8UL + 6UL;
    unsigned long lastEngineSpriteZOrder = SPRITE_ATTRIBUTES_START + 126UL * 8UL + 6UL;
    byte savedMouse[8], savedInspect[8], i;
    byte* updatePointer;
    byte pass, pending;
    for (i = 0; i < 8; ++i) {
        savedMouse[i] = testPeek(mouseAttributes + i);
        savedInspect[i] = testPeek(inspectAttributes + i);
    }
    for (pass = 0; pass < 2; ++pass) {
        bAResetViews();
        memCpyBankedBetween(baselineRam, CONTROLLER_BANK, allocationArray,
            MEMORY_MANAGEMENT_BANK, sizeof(baselineRam));
        captureVramBaseline();
        makeResizeFixture();
        testDisableInterrupts();
        blitAndPublish(0, 0, FALSE);
        blitAndPublish(0, 1, FALSE);
        blitAndPublish(0, 0, TRUE);
        oldAddress = activeBackBufferAddress(0);
        queueBlitUpdate(0, 1, TRUE, TRUE);
        bETerminateSpriteBuffer();
        pending = bERetirementCount;
        auditCheck(pending == 1, "reset state");
        checkVramBlock(oldAddress, TRUE, "old unowned");
        auditCheck(testPeek(SPRITE_ATTRIBUTES_START + 14UL) != 0, "not displayed");

        for (i = 0; i < 8; ++i) {
            testPoke(0xa0 + i, mouseAttributes + i);
            testPoke(0x50 + i, inspectAttributes + i);
        }
        testPoke(0, firstSpriteZOrder);
        testPoke(0x28, laterSpriteZOrder);
        testPoke(0x38, lastEngineSpriteZOrder);
        auditCheck(testPeek(firstSpriteZOrder) == 0 &&
            testPeek(laterSpriteZOrder) == 0x28 &&
            testPeek(lastEngineSpriteZOrder) == 0x38, "reset gap fixture");

        bAResetViews();
        auditCheck((testStatus() & 4) != 0, "IRQ state");
        auditCheck(testPeek(SPRITE_ATTRIBUTES_START + 14UL) == 0 &&
            testPeek(laterSpriteZOrder) == 0 &&
            testPeek(lastEngineSpriteZOrder) == 0, "reset left enabled sprite");
        for (i = 0; i < 8; ++i)
            auditCheck(testPeek(mouseAttributes + i) == (byte)(0xa0 + i) &&
                testPeek(inspectAttributes + i) == (byte)(0x50 + i),
                "reset changed reserved attributes");
        memCpyBanked((byte*)&updatePointer, (byte*)&bESpritesUpdatedBufferPointer,
            SPRITE_UPDATED_BANK, sizeof(updatePointer));
        auditCheck(updatePointer == bESpritesUpdatedBuffer, "update pending");
        pending = bERetirementCount;
        auditCheck(!pending, "queue pending");
        checkVramBlock(oldAddress, FALSE, "reset kept old allocation");

		publishUpdates();
		for (i = 0; i < 8; ++i)
			auditCheck(testPeek(mouseAttributes + i) == (byte)(0xa0 + i) &&
				testPeek(inspectAttributes + i) == (byte)(0x50 + i),
				"empty IRQ changed reserved attributes");
		queueBlitUpdate(0, 0, FALSE, TRUE);
		bETerminateSpriteBuffer();
		publishUpdates();
		auditCheck(testPeek(firstSpriteZOrder) != 0 && testPeek(laterSpriteZOrder) == 0 &&
			testPeek(lastEngineSpriteZOrder) == 0, "IRQ left reset sprite enabled");
		for (i = 0; i < 8; ++i)
			auditCheck(testPeek(mouseAttributes + i) == (byte)(0xa0 + i) &&
				testPeek(inspectAttributes + i) == (byte)(0x50 + i),
				"IRQ changed reserved attributes");
		bAResetViews();
		auditCheck(testPeek(laterSpriteZOrder) == 0 &&
			testPeek(lastEngineSpriteZOrder) == 0, "second reset left sprite enabled");
		for (i = 0; i < 8; ++i) {
			testPoke(savedMouse[i], mouseAttributes + i);
			testPoke(savedInspect[i], inspectAttributes + i);
		}
        testEnableInterrupts();
        b9DiscardView(244);
        compareRamToBaseline();
        compareVramToBaseline();
    }
}
#pragma rodata-name (pop)
#pragma code-name (pop)

#pragma code-name (push, "BANKRAM08")
#pragma rodata-name (push, "BANKRAM08")
void makeResizeFixture(void)
{
    byte smallRow[] = {0x18, 0};
    byte wideRow[] = {0x1f, 0x1f, 0x1f, 0x1f, 0x14, 0};
    View view;
    Loop loops[2];
    Cel cels[2];
    byte* loopStorage;
    byte* celStorage[2];
    byte* code;
    byte loopBank, celBanks[2], codeBank;
    byte row;

    memset(&view, 0, sizeof(view));
    memset(loops, 0, sizeof(loops));
    memset(cels, 0, sizeof(cels));
    memset(&fixtureTable, 0, sizeof(fixtureTable));

    view.loaded = TRUE;
    view.numberOfLoops = 2;
    view.maxCels = 1;
    view.maxVeraSlots = 1;
    loopStorage = b10BankedAlloc(2 * sizeof(Loop), &loopBank);
    code = b10BankedAlloc(64 + 64 * sizeof(wideRow), &codeBank);
    view.loops = (Loop*)loopStorage;
    view.loopsBank = loopBank;
    view.codeBlock = code;
    view.codeBlockBank = codeBank;

    cels[0].width = cels[0].height = 8;
    cels[0].splitSegments = cels[0].veraSlotsWidth = cels[0].veraSlotsHeight = 1;
    cels[0].bmp = code;
    cels[0].bitmapBank = codeBank;
    celStorage[0] = b10BankedAlloc(sizeof(Cel), &celBanks[0]);
    loops[0].numberOfCels = 1;
    loops[0].allocationWidth = loops[0].allocationHeight = SPR_ATTR_8;
    loops[0].cels = (Cel*)celStorage[0];
    loops[0].celsBank = celBanks[0];

    cels[1].width = cels[1].height = 64;
    cels[1].splitSegments = cels[1].veraSlotsWidth = cels[1].veraSlotsHeight = 1;
    cels[1].bmp = code + 64;
    cels[1].bitmapBank = codeBank;
    celStorage[1] = b10BankedAlloc(sizeof(Cel), &celBanks[1]);
    loops[1].numberOfCels = 1;
    loops[1].allocationWidth = loops[1].allocationHeight = SPR_ATTR_64;
    loops[1].cels = (Cel*)celStorage[1];
    loops[1].celsBank = celBanks[1];

    for (row = 0; row < 8; ++row)
        memCpyBanked(code + row * sizeof(smallRow), smallRow, codeBank, sizeof(smallRow));
    for (row = 0; row < 64; ++row)
        memCpyBanked(code + 64 + row * sizeof(wideRow), wideRow, codeBank, sizeof(wideRow));

    setLoadedCel(&loops[0], &cels[0], 0);
    setLoadedCel(&loops[1], &cels[1], 0);
    setLoadedLoop(&view, &loops[0], 0);
    setLoadedLoop(&view, &loops[1], 1);
    setLoadedView(&view, 244);

    fixtureTable.currentView = 244;
    fixtureTable.currentLoop = fixtureTable.currentCel = 0;
    fixtureTable.numberOfLoops = 2;
    fixtureTable.numberOfCels = 1;
    fixtureTable.xPos = 10;
    fixtureTable.yPos = 80;
    fixtureTable.priority = 15;
    setViewTab(&fixtureTable, 0);
}
#pragma rodata-name (pop)
#pragma code-name (pop)

#pragma code-name (push, "BANKRAM0E")
#pragma rodata-name (push, "RODATA")
void configureSplitResizeFixture(void)
{
    byte splitRow[] = {0x1f, 0x1f, 0x1f, 0x1f, 0x15, 0};
    View view;
    Loop loop;
    Cel cel;
    byte row;
    getLoadedView(&view, 244);
    view.maxVeraSlots = 2;
    setLoadedView(&view, 244);
    getLoadedLoop(&view, &loop, 1);
    loop.allocationHeight = SPR_ATTR_8;
    setLoadedLoop(&view, &loop, 1);
    getLoadedCel(&loop, &cel, 0);
    cel.width = 65;
    cel.height = 8;
    cel.splitSegments = 2;
    cel.veraSlotsWidth = 2;
    cel.veraSlotsHeight = 1;
    for (row = 0; row < 8; ++row)
        memCpyBanked(cel.bmp + row * sizeof(splitRow), splitRow, cel.bitmapBank, sizeof(splitRow));
    setLoadedCel(&loop, &cel, 0);
}
#pragma rodata-name (pop)
#pragma code-name (pop)

#pragma code-name (push, "BANKRAM0A")
#pragma rodata-name (push, "RODATA")
void configureSplitOldBufferFixture(void)
{
    byte splitRow[] = {0x1f, 0x1f, 0x1f, 0x1f, 0x15, 0};
    View view;
    Loop loop;
    Cel cel;
    byte row;

    getLoadedView(&view, 244);
    view.maxVeraSlots = 2;
    setLoadedView(&view, 244);

    getLoadedLoop(&view, &loop, 0);
    loop.allocationWidth = SPR_ATTR_64;
    setLoadedLoop(&view, &loop, 0);
    getLoadedCel(&loop, &cel, 0);
    cel.width = 65;
    cel.height = 8;
    cel.splitSegments = 2;
    cel.veraSlotsWidth = 2;
    cel.veraSlotsHeight = 1;
    for (row = 0; row < 8; ++row)
        memCpyBanked(cel.bmp + row * sizeof(splitRow), splitRow, cel.bitmapBank, sizeof(splitRow));
    setLoadedCel(&loop, &cel, 0);

    getLoadedLoop(&view, &loop, 1);
    loop.allocationHeight = SPR_ATTR_16;
    setLoadedLoop(&view, &loop, 1);
    getLoadedCel(&loop, &cel, 0);
    cel.width = 65;
    cel.height = 9;
    cel.splitSegments = 2;
    cel.veraSlotsWidth = 2;
    cel.veraSlotsHeight = 1;
    for (row = 0; row < 9; ++row)
        memCpyBanked(cel.bmp + row * sizeof(splitRow), splitRow, cel.bitmapBank, sizeof(splitRow));
    setLoadedCel(&loop, &cel, 0);
}
#pragma rodata-name (pop)
#pragma code-name (pop)

#pragma code-name (push, "BANKRAM01")
#pragma rodata-name (push, "RODATA")
void repeatedResizeRetiresOldBuffers(void)
{
    byte i;
	byte remainingRetirements;
	bAResetViews();
    memCpyBankedBetween(baselineRam, CONTROLLER_BANK, allocationArray,
        MEMORY_MANAGEMENT_BANK, sizeof(baselineRam));
	makeResizeFixture();
	testDisableInterrupts();
	blitAndPublish(0, 0, FALSE);
	blitAndPublish(0, 1, FALSE);
	blitAndPublish(0, 0, TRUE);
	checkPublishedBackBufferForEntry(0, 1);
	captureVramBaseline();
	memCpyBankedBetween(warmRam, CONTROLLER_BANK, allocationArray,
		MEMORY_MANAGEMENT_BANK, sizeof(warmRam));
	steadyBackBufferBlitWorkload();
	compareVramToBaseline();
	compareRamToWarmCache();

	for (i = 0; i < 24; ++i) {
        blitAndPublish(0, 1, TRUE);
        checkPublishedBackBufferForEntry(0, 1);
		blitAndPublish(0, 0, TRUE);
		checkPublishedBackBufferForEntry(0, 1);
		testEnableInterrupts();
		b9AnimateObjects();
		publishUpdates();
		testDisableInterrupts();
		compareVramToBaseline();
		compareRamToWarmCache();
	}
    testEnableInterrupts();
    b9AnimateObjects();
    testDisableInterrupts();
    remainingRetirements = bERetirementCount;
    auditCheck(!remainingRetirements, "retire queue not empty");
    compareVramToBaseline();

    testDisableInterrupts();
    bAResetViews();
    testEnableInterrupts();
    b9DiscardView(244);
    compareRamToBaseline();
}
#pragma rodata-name (pop)
#pragma code-name (pop)

void backbufferAuditCases(void)
{
    auditOutput("BACKBUFFER RETIREMENT CASES; ");
    repeatedResizeRetiresOldBuffers();
    delayedPublicationRetirement();
    unpublishedResizeIsRejected();
    splitBackBufferRetiresEverySlot();
    replacementAllocationFailureRollsBack();
    roomResetCancelsPendingPublication();
}
#pragma code-name (pop)
