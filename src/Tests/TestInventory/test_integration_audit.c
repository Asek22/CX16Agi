#include "audit.h"
#include "../../commands.h"
#include "../../graphics.h"
#include "../../helpers.h"
#include "../../inventory.h"
#include "../../keyboard.h"
#include "../../paletteManager.h"
#include "../../sound.h"
#include "../../spriteAllocator.h"
#include "../../textLayer.h"
#include "../../view.h"
#include <stdio.h>
#include <string.h>

#pragma code-name (push, "BANKRAM01")
#pragma rodata-name (push, "BANKRAM01")

#pragma wrapped-call (push, trampoline, 0x0f)
extern void auditRuntimeFixture(void);
#pragma wrapped-call (pop)
#pragma wrapped-call (push, trampoline, 0x07)
extern void auditRuntimeCheckSpritePixels(void);
extern void auditRuntimeCheckRoomPixels(byte room);
#pragma wrapped-call (pop)
extern void testResetVsyncMetrics(void);
extern volatile unsigned int testVsyncIrqs;
extern volatile unsigned long testVsyncMaxGap, testVsyncMinGap;
extern unsigned int vSyncCounter;
extern unsigned int testVisibleFrames, testDetailVisibleFrames, testInspectVisibleFrames;
extern byte mainLoopCounter;
extern byte bDSpriteAllocTable[];
extern byte allocationArray[];
extern unsigned long inventoryAddress;
extern byte inventoryBuffer[], inventoryLength, inventoryWrite;
extern void inventoryTransfer(void);
extern unsigned int bBCh1Ticks;
extern void testPutKey(byte key);
extern byte __fastcall__ testPeek(unsigned long address);

#pragma wrapped-call (push, trampoline, 4)
extern void b4Status(void);
#pragma wrapped-call (pop)

#pragma wrapped-call (push, trampoline, 0x0f)
static void captureRoom(void);
static void compareRoom(void);
static void captureState(void);
static void compareState(void);
static void checkSpritesAnimated(void);
static void playAndReplaceSound(void);
static void inspectAndSelect(void);
static void showDialog(byte* message, byte bank);
#pragma wrapped-call (pop)

#pragma wrapped-call (push, trampoline, 8)
static void snapshotVsyncMetrics(unsigned int* irqs, unsigned long* minimum,
    unsigned long* maximum);
static void waitForFrames(unsigned int count);
static unsigned int visibleSpriteSlots(void);
static void seedReservedSprites(void);
static void checkRoomSpriteReferences(void);
static void reportCycles(const char* phase, unsigned int samples,
    unsigned long total, unsigned long worst);
#pragma wrapped-call (pop)

extern void b6RunInterpreterTick(void);
static unsigned long fullInterpreterTick(void);
#pragma wrapped-call (push, trampoline, SOUND_BANK)
extern void bBPlaySound(byte soundNum, byte endSoundFlag);
#pragma wrapped-call (pop)

#define SEQUENCE_COUNT 20
#define FRAME_WAIT_LIMIT 1000000UL
#define ROOM_BYTES 0x9600UL
#define ROOM_SNAPSHOT_BYTES 8000
#define ROOM_SNAPSHOT_COUNT 5
#define STATE_MAP_OFFSET 0
#define STATE_PALETTE_OFFSET 4096
#define STATE_ATTR_OFFSET (STATE_PALETTE_OFFSET + 512)
#define STATE_ATTR_BYTES 1024
#define STATE_SPRITE_ALLOC_OFFSET (STATE_ATTR_OFFSET + STATE_ATTR_BYTES)
#define STATE_SNAPSHOT_BYTES (STATE_SPRITE_ALLOC_OFFSET + TOTAL_REAL_BLOCKS)

static byte* roomSnapshots[ROOM_SNAPSHOT_COUNT];
static byte roomSnapshotBanks[ROOM_SNAPSHOT_COUNT];
static byte* stateSnapshot;
static byte stateSnapshotBank;
static byte* allocationSnapshot;
static byte allocationSnapshotBank;
static byte transferBuffer[80];
static byte compareBuffer[80];
static byte warmAllocationBaseline[ALLOCATION_ARRAY_SIZE];

#pragma code-name (push, "CODE")
static unsigned long cycles(void)
{
    byte a = *(volatile byte*)0x9fb8;
    byte b = *(volatile byte*)0x9fb9;
    byte c = *(volatile byte*)0x9fba;
    byte d = *(volatile byte*)0x9fbb;
    return ((unsigned long)d << 24) | ((unsigned long)c << 16) |
        ((unsigned)b << 8) | a;
}

#pragma code-name (pop)

#pragma code-name (push, "BANKRAM01")
#pragma rodata-name (push, "BANKRAM01")

#pragma code-name (push, "BANKRAM08")
#pragma rodata-name (push, "BANKRAM08")

static void snapshotVsyncMetrics(unsigned int* irqs, unsigned long* minimum,
    unsigned long* maximum)
{
    asm("php");
    asm("sei");
    *irqs = testVsyncIrqs;
    *minimum = testVsyncMinGap;
    *maximum = testVsyncMaxGap;
    asm("plp");
}

#pragma code-name (push, "CODE")
static unsigned long fullInterpreterTick(void)
{
	byte previousBank = RAM_BANK;
	unsigned long start, elapsed;
	RAM_BANK = MEKA_BANK;
	start = cycles();
    b6RunInterpreterTick();
	elapsed = cycles() - start;
	RAM_BANK = previousBank;
	return elapsed;
}
#pragma code-name (pop)

static void waitForFrames(unsigned int count)
{
    unsigned int previous;
    unsigned long start;
    while (count--) {
        previous = vSyncCounter;
        start = cycles();
        while (vSyncCounter == previous) {
            if (cycles() - start > FRAME_WAIT_LIMIT)
                auditCheck(FALSE, "bounded VSYNC wait timed out");
        }
    }
}

#pragma rodata-name (pop)
#pragma code-name (pop)

#pragma code-name (push, "BANKRAM0F")
#pragma rodata-name (push, "BANKRAM0F")

static void copyVeraToBank(unsigned long address, byte* destination,
    byte destinationBank, unsigned int length)
{
    unsigned int offset = 0, amount;
    while (offset < length) {
        amount = length - offset;
        if (amount > sizeof(transferBuffer)) amount = sizeof(transferBuffer);
        inventoryAddress = address + offset;
        inventoryLength = amount;
        inventoryWrite = FALSE;
        inventoryTransfer();
        memCpyBanked(destination + offset, inventoryBuffer, destinationBank, amount);
        offset += amount;
    }
}

static void compareVeraToBank(unsigned long address, byte* snapshot,
    byte snapshotBank, unsigned int length, const char* message)
{
    unsigned int offset = 0, amount;
    while (offset < length) {
        amount = length - offset;
        if (amount > sizeof(transferBuffer)) amount = sizeof(transferBuffer);
        memCpyBanked(compareBuffer, snapshot + offset, snapshotBank, amount);
        inventoryAddress = address + offset;
        inventoryLength = amount;
        inventoryWrite = FALSE;
        inventoryTransfer();
        auditCheck(!memcmp(inventoryBuffer, compareBuffer, amount), message);
        offset += amount;
    }
}

static void captureRoom(void)
{
    unsigned long offset = 0;
    unsigned int amount;
    byte i;
    for (i = 0; i < ROOM_SNAPSHOT_COUNT; ++i) {
        amount = ROOM_BYTES - offset;
        if (amount > ROOM_SNAPSHOT_BYTES) amount = ROOM_SNAPSHOT_BYTES;
        copyVeraToBank(offset, roomSnapshots[i], roomSnapshotBanks[i], amount);
        offset += amount;
    }
}

static void compareRoom(void)
{
    unsigned long offset = 0;
    unsigned int amount;
    byte i;
    for (i = 0; i < ROOM_SNAPSHOT_COUNT; ++i) {
        amount = ROOM_BYTES - offset;
        if (amount > ROOM_SNAPSHOT_BYTES) amount = ROOM_SNAPSHOT_BYTES;
        compareVeraToBank(offset, roomSnapshots[i], roomSnapshotBanks[i], amount,
            "full room bitmap changed across a warmed sequence");
        offset += amount;
    }
}

static void captureState(void)
{
    memCpyBankedBetween(stateSnapshot + STATE_SPRITE_ALLOC_OFFSET,
        stateSnapshotBank, bDSpriteAllocTable, SPRITE_MEMORY_MANAGER_BANK,
        TOTAL_REAL_BLOCKS);
    copyVeraToBank(MAPBASE, stateSnapshot + STATE_MAP_OFFSET,
        stateSnapshotBank, 4096);
    copyVeraToBank(PALETTE_START, stateSnapshot + STATE_PALETTE_OFFSET,
        stateSnapshotBank, 512);
    copyVeraToBank(SPRITE_ATTRIBUTES_START, stateSnapshot + STATE_ATTR_OFFSET,
        stateSnapshotBank, STATE_ATTR_BYTES);
    memCpyBankedBetween(allocationSnapshot, allocationSnapshotBank,
        allocationArray, MEMORY_MANAGEMENT_BANK, ALLOCATION_ARRAY_SIZE);
}

static void compareState(void)
{
    unsigned int offset, amount, i, spriteBytes = 0;
    for (offset = 0; offset < TOTAL_REAL_BLOCKS; offset += sizeof(transferBuffer)) {
        amount = TOTAL_REAL_BLOCKS - offset;
        if (amount > sizeof(transferBuffer)) amount = sizeof(transferBuffer);
        memCpyBanked(compareBuffer, stateSnapshot + STATE_SPRITE_ALLOC_OFFSET + offset,
            stateSnapshotBank, amount);
        memCpyBanked(transferBuffer, bDSpriteAllocTable + offset,
            SPRITE_MEMORY_MANAGER_BANK, amount);
        auditCheck(!memcmp(transferBuffer, compareBuffer, amount),
            "sprite VRAM allocation ownership changed");
        for (i = 0; i < amount; ++i) spriteBytes += transferBuffer[i] != 0;
    }
    auditCheck(spriteBytes != 0, "warmed sprite allocation table became empty");

    for (offset = 0; offset < ALLOCATION_ARRAY_SIZE; offset += sizeof(transferBuffer)) {
        amount = ALLOCATION_ARRAY_SIZE - offset;
        if (amount > sizeof(transferBuffer)) amount = sizeof(transferBuffer);
        memCpyBanked(compareBuffer, allocationSnapshot + offset,
            allocationSnapshotBank, amount);
        memCpyBanked(transferBuffer, allocationArray + offset,
            MEMORY_MANAGEMENT_BANK, amount);
        auditCheck(!memcmp(transferBuffer, compareBuffer, amount),
            "banked RAM ownership changed across a sequence");
    }

    compareVeraToBank(MAPBASE, stateSnapshot + STATE_MAP_OFFSET,
        stateSnapshotBank, 7 * TILE_LAYER_BYTES_PER_ROW, "full text map or attributes changed");
    compareVeraToBank(MAPBASE + 8UL * TILE_LAYER_BYTES_PER_ROW,
        stateSnapshot + STATE_MAP_OFFSET + 8 * TILE_LAYER_BYTES_PER_ROW,
        stateSnapshotBank, 4096 - 8 * TILE_LAYER_BYTES_PER_ROW,
        "full text map or attributes changed");
    compareVeraToBank(PALETTE_START, stateSnapshot + STATE_PALETTE_OFFSET,
        stateSnapshotBank, 512, "full palette changed across a sequence");
    compareVeraToBank(SPRITE_ATTRIBUTES_START, stateSnapshot + STATE_ATTR_OFFSET,
        stateSnapshotBank, STATE_ATTR_BYTES,
        "full sprite attribute table changed across a sequence");
}

#pragma rodata-name (pop)
#pragma code-name (pop)

#pragma code-name (push, "BANKRAM08")
#pragma rodata-name (push, "BANKRAM08")

static unsigned int visibleSpriteSlots(void)
{
    unsigned int slot, count = 0;
    for (slot = 1; slot < 127; ++slot)
        if (testPeek(SPRITE_ATTRIBUTES_START + (unsigned long)slot * 8 + 6) & 12)
            ++count;
    return count;
}

static void seedReservedSprites(void)
{
    static const byte slots[] = {0, 127};
    byte i;
    for (i = 0; i < sizeof(slots); ++i) {
        unsigned long address = SPRITE_ATTRIBUTES_START + (unsigned long)slots[i] * 8;
        unsigned int x = slots[i] ? 320 : 0, y = slots[i] ? 240 : 0;
        testPoke(0, address);
        testPoke(0, address + 1);
        testPoke(x, address + 2);
        testPoke(x >> 8, address + 3);
        testPoke(y, address + 4);
        testPoke(y >> 8, address + 5);
        testPoke(12, address + 6);
        testPoke(slots[i] ? 0xf0 : 0, address + 7);
    }
}

static void checkRoomSpriteReferences(void)
{
    unsigned int i;
    ViewTable table;
    ViewTableMetadata metadata;

    auditCheck((testPeek(SPRITE_ATTRIBUTES_START + 6) & 12) == 12 &&
        (testPeek(SPRITE_ATTRIBUTES_START + 127UL * 8 + 6) & 12) == 12,
        "room change cleared the reserved mouse or inspect sprite");
    for (i = 0; i < SPRITE_SLOTS; ++i) {
        if (i >= 1 && i <= 4) continue;
        memCpyBanked((byte*)&metadata, (byte*)&viewTableMetadata[i],
            SPRITE_METADATA_BANK, sizeof(metadata));
        auditCheck(!metadata.loopsVeraAddressesPointers && !metadata.veraAddresses &&
            !metadata.inactive && !metadata.backBuffers,
            "room change retained stale sprite metadata");
        getViewTab(&table, i);
        auditCheck(!(table.flags & (DRAWN | ANIMATED)),
            "room change retained a displayed sprite reference");
    }
}

#pragma rodata-name (pop)
#pragma code-name (pop)

#pragma code-name (push, "BANKRAM0F")
#pragma rodata-name (push, "BANKRAM0F")

static void checkSpritesAnimated(void)
{
    static const byte expectedLoop[4] = {0, 1, 2, 0};
    static const word expectedX[4] = {140, 38, 80, 120};
    ViewTable table;
    ViewTableMetadata metadata;
    byte i;
    for (i = 1; i <= 4; ++i) {
        getViewTab(&table, i);
        memCpyBanked((byte*)&metadata, (byte*)&viewTableMetadata[i],
            SPRITE_METADATA_BANK, sizeof(metadata));
        auditCheck(table.currentView == 0 && table.numberOfLoops == 3 &&
            table.numberOfCels == 1 && metadata.viewNum == 0,
            "room script did not retain the loaded animated VIEW");
        auditCheck(table.currentLoop == expectedLoop[i - 1],
            "room script selected the wrong mirrored VIEW loop");
        auditCheck(table.xPos == expectedX[i - 1],
            "room script positioned an animated VIEW object incorrectly");
        auditCheck((table.flags & (DRAWN | ANIMATED)) == (DRAWN | ANIMATED),
            "room script did not draw and animate its VIEW object");
        auditCheck(metadata.loopsVeraAddressesPointers && metadata.veraAddresses,
            "room script lost the sprite's real VRAM display reference");
    }
    auditCheck(visibleSpriteSlots() >= 4,
        "full runtime workload did not publish several visible sprites");
}

static unsigned int soundTicks(void)
{
    unsigned int ticks;
    memCpyBanked((byte*)&ticks, (byte*)&bBCh1Ticks, SOUND_BANK, sizeof(ticks));
    return ticks;
}

static void playAndReplaceSound(void)
{
    unsigned int before, after;
    flag[60] = FALSE;
    flag[61] = FALSE;
    bBPlaySound(0, 60);
    waitForFrames(1);
    before = soundTicks();
    auditCheck(before != 0, "first real SOUND did not enter its note IRQ");
    bBPlaySound(1, 61);
    auditCheck(flag[60] && !flag[61],
        "real SOUND replacement did not transfer completion flags");
    waitForFrames(1);
    before = soundTicks();
    waitForFrames(1);
    after = soundTicks();
    auditCheck(before != 0 && after != 0 && after < before && testPeek(0x1f9c2UL) != 0,
        "replacement SOUND did not advance through the active PSG IRQ path");
}

static void inspectAndSelect(void)
{
    testDetailVisibleFrames = 0;
    testVisibleFrames = 0;
    testInspectVisibleFrames = 0;
    testPutKey('v');
    testPutKey('x');
    testPutKey(KEY_DOWN);
    testPutKey(KEY_ENTER);
    b4Status();
    auditCheck(var[25] == 1 && !inventoryActive,
        "real inventory inspect and selection did not return object one");
    auditCheck(testVisibleFrames != 0 && testInspectVisibleFrames >= 2,
        "built-in inspect art did not remain visible through real VSYNCs");
}

static void showDialog(byte* message, byte bank)
{
    testPutKey(KEY_ESC);
    auditCheck(b3DisplayManuallyWrappedMessageBox((char*)message, bank,
        AUTO_CALC_ROW, AUTO_CALC_COLUMN, TEXTBOX_PALETTE_NUMBER, 20, 6) == KEY_ESC,
        "real dialog did not consume its queued close key");
}

#pragma rodata-name (pop)
#pragma code-name (pop)

#pragma code-name (push, "BANKRAM08")
#pragma rodata-name (push, "BANKRAM08")

static void reportCycles(const char* phase, unsigned int samples,
    unsigned long total, unsigned long worst)
{
    char line[96];
    sprintf(line, "%s samples=%u average=%lu worst=%lu cycles\n",
        phase, samples, total / samples, worst);
    auditOutput(line);
}

#pragma rodata-name (pop)
#pragma code-name (pop)

#pragma rodata-name (pop)
#pragma code-name (pop)

void integrationAuditPrepare(void)
{
    auditRuntimeFixture();
}

void integrationAuditCases(void)
{
    static const char dialogText[] = "A ROOM CHANGED.\012THE CACHE IS WARM.";
    byte room, messageBank, *message;
    unsigned int i, vsyncIrqs;
    unsigned long roomTotal = 0, roomWorst = 0;
    unsigned long animationTotal = 0, animationWorst = 0;
    unsigned long allTotal = 0, allWorst = 0;
    unsigned long roomFrameCycles, animationFrameCycles;
    unsigned long minimumGap, maximumGap;
    byte ticksBefore;
    char line[100];

    seedReservedSprites();
    b4SetLastRoomLoaded(0);
    auditCheck(b4GetLastRoomLoaded() == 0,
        "integration room baseline did not reset its prior-room reference");
    testResetVsyncMetrics();
    ticksBefore = mainLoopCounter;

    flag[5] = FALSE;
    roomFrameCycles = fullInterpreterTick();

    memCpyBanked(warmAllocationBaseline, allocationArray,
        MEMORY_MANAGEMENT_BANK, ALLOCATION_ARRAY_SIZE);
    for (i = 0; i < ROOM_SNAPSHOT_COUNT; ++i) {
        roomSnapshots[i] = b10BankedAlloc(ROOM_SNAPSHOT_BYTES,
            &roomSnapshotBanks[i]);
        auditCheck(roomSnapshots[i] != NULL,
            "room VRAM snapshot allocation failed");
    }
    stateSnapshot = b10BankedAlloc(8000, &stateSnapshotBank);
    auditCheck(stateSnapshot != NULL, "integration state snapshot allocation failed");
    allocationSnapshot = b10BankedAlloc(ALLOCATION_ARRAY_SIZE,
        &allocationSnapshotBank);
    auditCheck(allocationSnapshot != NULL,
        "integration RAM ownership snapshot allocation failed");
    message = b10BankedAlloc(sizeof(dialogText), &messageBank);
    auditCheck(message != NULL, "real dialog resource allocation failed");
    memCpyBankedBetween(message, messageBank, (byte*)dialogText, 0x01, sizeof(dialogText));
    captureRoom();
    captureState();

    for (i = 0; i < SEQUENCE_COUNT; ++i) {
        room = (byte)(i + 1);
        flag[5] = FALSE;
        var[250] = room;
        waitForFrames(6);
        roomFrameCycles = fullInterpreterTick();
        roomTotal += roomFrameCycles;
        if (roomFrameCycles > roomWorst) roomWorst = roomFrameCycles;
        allTotal += roomFrameCycles;
        if (roomFrameCycles > allWorst) allWorst = roomFrameCycles;
        waitForFrames(1);
        auditCheck(var[0] == room,
            "logic-driven new.room.v did not enter its requested room");
        auditCheck(var[251] == room,
            "room logic did not resume after new.room.v");
        auditRuntimeCheckRoomPixels(room);
        checkSpritesAnimated();
        checkRoomSpriteReferences();

        waitForFrames(6);
        animationFrameCycles = fullInterpreterTick();
        animationTotal += animationFrameCycles;
        if (animationFrameCycles > animationWorst) animationWorst = animationFrameCycles;
        allTotal += animationFrameCycles;
        if (animationFrameCycles > allWorst) allWorst = animationFrameCycles;
        waitForFrames(1);
        auditCheck(var[251] == room,
            "room logic did not resume and draw after new.room.v");
        auditRuntimeCheckRoomPixels(room);
        checkSpritesAnimated();
        auditRuntimeCheckSpritePixels();
        captureRoom();
        captureState();

        inspectAndSelect();
        compareState();
        showDialog(message, messageBank);
        compareRoom();
        compareState();

        playAndReplaceSound();
        compareState();

        sprintf(line, "SEQUENCE %u room=%u frame-room=%lu frame-animation=%lu cycles\n",
            i + 1, room, roomFrameCycles, animationFrameCycles);
        auditOutput(line);
    }

    auditCheck(mainLoopCounter == (byte)(ticksBefore + 1 + SEQUENCE_COUNT * 2),
        "long integration did not execute both production ticks in every sequence");
    snapshotVsyncMetrics(&vsyncIrqs, &minimumGap, &maximumGap);
    auditCheck(vsyncIrqs >= SEQUENCE_COUNT * 12,
        "long integration did not observe repeated real VSYNC service");
    auditCheck(minimumGap != 0 && maximumGap >= minimumGap,
        "long integration did not capture a real IRQ service gap");
    reportCycles("ROOM CHANGE TICK", SEQUENCE_COUNT, roomTotal, roomWorst);
    reportCycles("ANIMATION TICK", SEQUENCE_COUNT, animationTotal, animationWorst);
    reportCycles("LONG-SEQUENCE FULL TICK", SEQUENCE_COUNT * 2,
        allTotal, allWorst);
    sprintf(line, "INTEGRATION SOUND VSYNC samples=%u minimum-entry-gap=%lu maximum-entry-gap=%lu excess=%lu cycles\n",
        vsyncIrqs, minimumGap, maximumGap, maximumGap - minimumGap);
    auditOutput(line);

    auditCheck(b10BankedDealloc(message, messageBank),
        "integration dialog fixture cleanup");
    auditCheck(b10BankedDealloc(stateSnapshot, stateSnapshotBank),
        "integration state snapshot cleanup");
    auditCheck(b10BankedDealloc(allocationSnapshot, allocationSnapshotBank),
        "integration RAM ownership snapshot cleanup");
    for (i = 0; i < ROOM_SNAPSHOT_COUNT; ++i)
        auditCheck(b10BankedDealloc(roomSnapshots[i], roomSnapshotBanks[i]),
            "integration room snapshot cleanup");
    {
        unsigned int offset, amount;
        for (offset = 0; offset < ALLOCATION_ARRAY_SIZE; offset += sizeof(compareBuffer)) {
            amount = ALLOCATION_ARRAY_SIZE - offset;
            if (amount > sizeof(compareBuffer)) amount = sizeof(compareBuffer);
            memCpyBanked(compareBuffer, allocationArray + offset,
                MEMORY_MANAGEMENT_BANK, amount);
            auditCheck(!memcmp(warmAllocationBaseline + offset, compareBuffer, amount),
                "twenty room sequences leaked banked RAM after fixture cleanup");
        }
    }
    auditOutput("INTEGRATION AUDIT PASS\n");
}

#pragma rodata-name (pop)
#pragma code-name (pop)
