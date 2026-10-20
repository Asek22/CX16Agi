#include "audit.h"
#include "../../commands.h"
#include "../../logic.h"
#include "../../memoryManager.h"
#include "../../view.h"
#include "../../fixed.h"
#include <stdio.h>
#include <string.h>
#pragma wrapped-call (push, trampoline, SPRITE_METADATA_BANK)
extern boolean bESetViewMetadata(View* view, ViewTable* table, byte viewNum, byte tableNum, byte slot);
extern void bEResetViewTableMetadata(void), bEResetSpritesUpdatedBuffer(void), bEResetSpritePointers(void);
#pragma wrapped-call (pop)
#pragma wrapped-call (push, trampoline, LINE_DRAW_BANK)
extern void b8SetupLineTables(void);
#pragma wrapped-call (pop)
extern boolean agiBlit(byte entry, boolean disableInterrupts);
extern void setLoadedLoop(View* view, Loop* loop, byte number);
extern void setLoadedCel(Loop* loop, Cel* cel, byte number);
extern byte bESpritesUpdatedBuffer[];
extern void testDisableInterrupts(void), testEnableInterrupts(void);
extern byte priorityBase, b9PreComputedPriority[];
extern byte testStatus(void);
extern byte mainLoopCounter;
extern byte horizon;
extern LOGICEntry logics[];
extern LOGICFile logicFiles[];
extern byte* ZP_CURRENTLY_PLAYING_NOTE_1;
#pragma zpsym("ZP_CURRENTLY_PLAYING_NOTE_1")

static void priorityPopulationCase(void);
static void priority20AuditCasesBanked(void);
#pragma wrapped-call (push, trampoline, 0x02)
static void coldBulkSplit(View* view, Loop* loop, Cel* cel, ViewTable* table, byte flipped);
#pragma wrapped-call (pop)
#pragma wrapped-call (push, trampoline, 0x0f)
static void castleFlagViewCase(void);
#pragma wrapped-call (pop)

#pragma wrapped-call (push, trampoline, VIEW_CODE_BANK_1)
extern void b9DiscardView(byte viewNum), b9AnimateObjects(void);
#pragma wrapped-call (pop)

#define PRIORITY_POPULATE_BASELINE_CYCLES 1712466UL
#define PRIORITY_POPULATE_CYCLE_LIMIT (PRIORITY_POPULATE_BASELINE_CYCLES * 3UL / 4)

#pragma code-name (push, "BANKRAM02")
#pragma rodata-name (push, "BANKRAM02")
static void coldBulkSplit(View* view, Loop* loop, Cel* cel, ViewTable* table, byte flipped)
{
    ViewTableMetadata metadata;
    VeraSpriteAddress addresses[4];
    byte updates[30], i, row;
    unsigned long first, second, third, fourth;

    getLoadedCel(loop, cel, 0);
    cel->flipped = flipped;
    setLoadedCel(loop, cel, 0);
    table->staleCounter = 0;
    setViewTab(table, 0);
    bEResetViewTableMetadata(); bEResetSpritePointers(); bEResetSpritesUpdatedBuffer();
    bESetViewMetadata(view, table, 247, 0, 0);
    memCpyBanked((byte*)&metadata, (byte*)viewTableMetadata, SPRITE_METADATA_BANK, sizeof(metadata));
    memCpyBanked((byte*)addresses, (byte*)metadata.veraAddresses, metadata.viewTableMetadataBank, sizeof(addresses));
    auditCheck(addresses[0] == 0 && addresses[1] == 0 && addresses[2] == 0 && addresses[3] == 0,
               "cold split fixture started with a cached VERA address");

    bDResetSpriteMemoryManager();
    for (row = 16; row <= 80; ++row) {
        for (i = 0; i < 80; ++i) testPoke(0x44, PRIORITY_START + 80UL * row + i);
        if (flipped) testPoke(0xf4, PRIORITY_START + 80UL * row + 5);
        else for (i = 5; i < 21; ++i) testPoke(0xff, PRIORITY_START + 80UL * row + i);
    }

    testDisableInterrupts();
    auditCheck(agiBlit(0, FALSE), "cold split priority fixture rejected valid cel");
    memCpyBanked(updates, bESpritesUpdatedBuffer, SPRITE_UPDATED_BANK, sizeof(updates));
    first = ((unsigned long)updates[0] | ((unsigned long)updates[1] << 8)) * 32;
    second = ((unsigned long)updates[7] | ((unsigned long)updates[8] << 8)) * 32;
    third = ((unsigned long)updates[14] | ((unsigned long)updates[15] << 8)) * 32;
    fourth = ((unsigned long)updates[21] | ((unsigned long)updates[22] << 8)) * 32;

    if (flipped) {
        auditCheck(updates[2] == 22 && updates[3] == 0 && updates[9] == 214 && updates[10] == 255 &&
                   updates[16] == 22 && updates[17] == 0 && updates[23] == 214 && updates[24] == 255,
                   "cold mirrored split segments lost their actual screen positions");
        auditCheck(testPeek(first) == 0x11 && testPeek(first + 31) == 0x11 &&
                   testPeek(third) == 0x11 && testPeek(third + 31) == 0x11,
                   "cold mirrored full segment lost visible endpoint pixels");
        auditCheck(testPeek(second) == 0 && testPeek(fourth) == 0,
                   "COLD MIRRORED PARTIAL SPLIT SAMPLED PRIORITY USING THE WHOLE CEL WIDTH");
        auditCheck(testPeek(second + 1) == 0 && testPeek(fourth + 1) == 0,
                   "cold mirrored partial split drew outside its one-pixel width");
    } else {
        auditCheck(updates[2] == 20 && updates[3] == 0 && updates[9] == 84 && updates[10] == 0 &&
                   updates[16] == 20 && updates[17] == 0 && updates[23] == 84 && updates[24] == 0,
                   "cold split segments lost their actual screen positions");
        auditCheck(testPeek(first) == 0 && testPeek(third) == 0,
                   "cold split left segment ignored foreground priority");
        auditCheck(testPeek(second) == 0x11 && testPeek(fourth) == 0x11,
                   "COLD SPLIT COLUMN READ PRIORITY FROM THE OBJECT ORIGIN");
        auditCheck(testPeek(second + 1) == 0 && testPeek(fourth + 1) == 0,
                   "cold split partial segment drew outside its one-pixel width");
    }
    testEnableInterrupts();
}

#pragma rodata-name (pop)
#pragma code-name (pop)
#pragma code-name (push, "CODE")
static unsigned long priorityCycles(void)
{
    byte a = *(volatile byte*)0x9fb8, b = *(volatile byte*)0x9fb9;
    byte c = *(volatile byte*)0x9fba, d = *(volatile byte*)0x9fbb;
    return ((unsigned long)d << 24) | ((unsigned long)c << 16) | ((unsigned)b << 8) | a;
}
#pragma code-name (pop)

#pragma code-name (push, "BANKRAM0F")
#pragma rodata-name (push, "BANKRAM0F")
#pragma bss-name (push, "BANKRAM0F")
static byte castleFlagResource[320];
static const byte castleFlagLogic[] = {
    63, 50,          /* set.horizon(50) */
    30, 247,         /* load.view(247) */
    33, 1,           /* animate.obj(o1) */
    41, 1, 247,       /* set.view(o1, 247) */
    37, 1, 5, 17,     /* position(o1, 5, 17) */
    61, 1,            /* ignore.horizon(o1) */
    54, 1, 15,        /* set.priority(o1, 15) */
    35, 1,            /* draw(o1) */
    0                 /* return */
};

static unsigned int writeCastleFlagCel(byte* output, byte frame)
{
    byte y, rightStart, gap;
    unsigned int outputSize = 3;
    output[0] = 42; output[1] = 10; output[2] = 0;
    for (y = 0; y < 10; ++y) {
        rightStart = 36 - (y & 1);
        gap = rightStart - 5;
        output[outputSize++] = ((frame + 1) << 4) | 5; /* first pennant, x=0..4 */
        while (gap > 15) {
            output[outputSize++] = 0x0f;
            gap -= 15;
        }
        if (gap) output[outputSize++] = gap;
        output[outputSize++] = ((frame + 6) << 4) | (42 - rightStart);
        output[outputSize++] = 0;
    }
    return outputSize;
}

static void writeCastleFlagView(AGIFilePosType* previous)
{
    byte volumeHeader[5] = {0x12, 0x34, 0, 0, 0};
    AGIFilePosType position;
    FILE* output;
    byte cel;
    unsigned int celOffset = 11, resourceSize, celSize;

    memset(castleFlagResource, 0, sizeof(castleFlagResource));
    castleFlagResource[2] = 1;
    castleFlagResource[5] = 7;
    castleFlagResource[7] = 5;
    for (cel = 0; cel < 5; ++cel) {
        auditCheck(7 + celOffset + 60 <= sizeof(castleFlagResource),
            "CASTLE FLAG CEL exceeds fixture buffer");
        castleFlagResource[8 + cel * 2] = (byte)celOffset;
        castleFlagResource[9 + cel * 2] = (byte)(celOffset >> 8);
        celSize = writeCastleFlagCel(castleFlagResource + 7 + celOffset, cel);
        celOffset += celSize;
    }
    resourceSize = celOffset + 7;
    auditCheck(resourceSize <= sizeof(castleFlagResource), "CASTLE FLAG VIEW fixture buffer overflow");
    volumeHeader[3] = (byte)resourceSize;
    volumeHeader[4] = (byte)(resourceSize >> 8);
    output = fopen("vol.8", "wb");
    auditCheck(output != 0, "CASTLE FLAG VIEW fixture creation");
    auditCheck(fwrite(volumeHeader, 1, sizeof(volumeHeader), output) == sizeof(volumeHeader),
        "CASTLE FLAG volume header write");
    auditCheck(fwrite(castleFlagResource, 1, resourceSize, output) == resourceSize,
        "CASTLE FLAG VIEW resource write");
    auditCheck(fclose(output) == 0, "CASTLE FLAG VIEW fixture close");

    memCpyBanked((byte*)previous, (byte*)&viewdir[247], DIRECTORY_BANK, sizeof(*previous));
    memset(&position, 0, sizeof(position));
    position.fileNum = 8;
    memCpyBanked((byte*)&viewdir[247], (byte*)&position, DIRECTORY_BANK, sizeof(position));
}

static unsigned long visibleCastleSprite(word x, word y)
{
    unsigned int slot;
    unsigned long attributes;
    word spriteX, spriteY;
    for (slot = 1; slot < 127; ++slot) {
        attributes = SPRITE_ATTRIBUTES_START + (unsigned long)slot * 8;
        if ((testPeek(attributes + 6) & 12) != 8) continue;
        spriteX = testPeek(attributes + 2) | ((word)testPeek(attributes + 3) << 8);
        spriteY = testPeek(attributes + 4) | ((word)testPeek(attributes + 5) << 8);
        if (spriteX == x && spriteY == y) {
            word address = testPeek(attributes) | ((word)testPeek(attributes + 1) << 8);
            return (unsigned long)(address & 0x0fff) << 5;
        }
    }
    return 0;
}

static void waitCastleVsync(void)
{
    unsigned int previous = vSyncCounter;
    unsigned long start = priorityCycles();
    while (vSyncCounter == previous) {
        if (priorityCycles() - start > 1000000UL)
            auditCheck(FALSE, "CASTLE FLAG VSYNC wait timed out");
    }
}

static void castleFlagViewCase(void)
{
    AGIFilePosType previous;
    LOGICEntry entry, savedEntry;
    LOGICFile file, savedFile;
    ViewTable table;
    byte frame, row, i, logicBank, oldHorizon = horizon, oldIrqDisabled = testStatus() & 4;
    byte* savedSoundPointer;
    byte* logicCode;
    byte mouseAttributes[8];
    unsigned long left, right;
    word expectedY = STARTING_ROW + 8;

    writeCastleFlagView(&previous);
    b5GetLogicEntry(&savedEntry, 250);
    b5GetLogicFile(&savedFile, 250);
    logicCode = b10BankedAlloc(sizeof(castleFlagLogic), &logicBank);
    auditCheck(logicCode != NULL, "CASTLE FLAG LOGIC fixture allocation");
    memCpyBankedBetween(logicCode, logicBank, (byte*)castleFlagLogic, 0x0f,
        sizeof(castleFlagLogic));
    memset(&entry, 0, sizeof(entry));
    memset(&file, 0, sizeof(file));
    entry.loaded = TRUE;
    entry.data = &logicFiles[250];
    file.logicCode = logicCode;
    file.codeBank = logicBank;
    file.codeSize = sizeof(castleFlagLogic);
    b5SetLogicEntry(&entry, 250);
    b5SetLogicFile(&file, 250);

    bAInitObjects();
    bDResetSpriteMemoryManager();
    bEResetViewTableMetadata(); bEResetSpritesUpdatedBuffer(); bEResetSpritePointers();
    executeLogic(&logics[250], 250);
    getViewTab(&table, 1);
    auditCheck(table.currentView == 247 && table.currentLoop == 0 && table.xPos == 5 &&
        table.yPos == 17 && table.priority == 15,
        "CASTLE FLAG SCRIPT DID NOT SET THE EXPECTED VIEW POSITION AND PRIORITY");
    auditCheck((table.flags & (DRAWN | ANIMATED | UPDATE | CYCLING | MOTION |
        FIXEDPRIORITY | IGNOREHORIZON)) == (DRAWN | ANIMATED | UPDATE | CYCLING |
        MOTION | FIXEDPRIORITY | IGNOREHORIZON),
        "CASTLE FLAG SCRIPT DID NOT SET DRAW, ANIMATION, CYCLE, OR PRIORITY FLAGS");
    auditCheck(table.cycleTime == 1 && table.cycleTimeCount == 1,
        "CASTLE FLAG SCRIPT DID NOT USE THE EXPECTED ANIMATION CYCLE");
    auditCheck(table.currentCel == 0,
        "CASTLE FLAG SCRIPT DID NOT START ON THE FIRST FLAG CEL");
    b8SetupLineTables();
    for (row = 8; row < 18; ++row)
        for (i = 0; i < 80; ++i)
            testPoke(0xff, PRIORITY_START + 80UL * row + i);
    for (i = 0; i < sizeof(mouseAttributes); ++i)
        mouseAttributes[i] = testPeek(SPRITE_ATTRIBUTES_START + i);

    for (frame = 0; frame < 5; ++frame) {
        byte expectedCel = (frame + 1) % 5;
        getViewTab(&table, 1);
        if (!frame) {
            testDisableInterrupts();
            savedSoundPointer = ZP_CURRENTLY_PLAYING_NOTE_1;
            /* This is the KQ1 channel-1 note pointer; splitting must ignore it. */
            ZP_CURRENTLY_PLAYING_NOTE_1 = (byte*)0xacf6;
            b9AnimateObjects();
            ZP_CURRENTLY_PLAYING_NOTE_1 = savedSoundPointer;
            if (!oldIrqDisabled) testEnableInterrupts();
        } else b9AnimateObjects();
        ++mainLoopCounter;
        waitCastleVsync();
        getViewTab(&table, 1);
        auditCheck(table.currentCel == expectedCel,
            "CASTLE FLAG ANIMATION DID NOT CYCLE THROUGH ITS FIVE CELS");
        left = visibleCastleSprite(10, expectedY);
        right = visibleCastleSprite(74, expectedY);
        auditCheck(left && right,
            "MISSING CASTLE FLAGS: PRIORITY-15 SPLIT SPRITES WERE NOT PUBLISHED");
        for (i = 0; i < sizeof(mouseAttributes); ++i)
            auditCheck(testPeek(SPRITE_ATTRIBUTES_START + i) == mouseAttributes[i],
                "CASTLE FLAG SPRITE OVERWROTE THE RESERVED MOUSE SLOT");
        auditCheck(testPeek(left) == ((expectedCel + 1) * 0x11) && testPeek(right) == 0 &&
            testPeek(left + 32) == ((expectedCel + 1) * 0x11) &&
            testPeek(right + 32 + 4) == ((expectedCel + 6) * 0x11),
            "CASTLE FLAG CEL LOST ITS SPARSE PIXELS ACROSS THE 32-PIXEL SPLIT");
    }

    getViewTab(&table, 1);
    table.flags &= ~(DRAWN | ANIMATED | UPDATE);
    setViewTab(&table, 1);
    bEResetSpritesUpdatedBuffer(); bEResetViewTableMetadata(); bEResetSpritePointers();
    bDResetSpriteMemoryManager();
    b9DiscardView(247);
    b5SetLogicEntry(&savedEntry, 250);
    b5SetLogicFile(&savedFile, 250);
    auditCheck(b10BankedDealloc(logicCode, logicBank), "CASTLE FLAG LOGIC fixture cleanup");
    horizon = oldHorizon;
    memCpyBanked((byte*)&viewdir[247], (byte*)&previous, DIRECTORY_BANK, sizeof(previous));
    auditCheck(remove("vol.8") == 0, "CASTLE FLAG VIEW fixture cleanup");
    if (!oldIrqDisabled) testEnableInterrupts();
}
#pragma bss-name (pop)
#pragma rodata-name (pop)
#pragma code-name (pop)

static void priorityPopulationCase(void)
{
    byte base = 48, actual, expected, i, savedBank = RAM_BANK;
    byte savedIrqDisabled = testStatus() & 4;
    byte digits = 0;
    unsigned repeat;
    unsigned long start, elapsed, cycleValue;
    char cycleDigits[11];

    priorityBase = base;
    bAPopulatePrecomputedPriorityTable();
    for (i = 0; i < CHAR_MAX; ++i) {
        memCpyBanked(&actual, b9PreComputedPriority + i, VIEW_CODE_BANK_1, 1);
        expected = MIN_PRIORITY;
        if (i >= base) expected = ((i - base) * 10 + (PICTURE_HEIGHT - base) * 5) / (PICTURE_HEIGHT - base);
        auditCheck(actual == expected, "precomputed priority table value mismatch");
    }

    testDisableInterrupts();
    start = priorityCycles();
    for (repeat = 0; repeat < 8; ++repeat) bAPopulatePrecomputedPriorityTable();
    elapsed = (priorityCycles() - start) / 8;
    cycleValue = elapsed;
    do { cycleDigits[digits++] = '0' + cycleValue % 10; cycleValue /= 10; } while (cycleValue);
    auditOutput("PRIORITY TABLE BASELINE CYCLES: 1712466; ");
    auditOutput("PRIORITY TABLE POPULATE CYCLES: ");
    while (digits) *(volatile byte*)0x9fbb = cycleDigits[--digits];
    auditOutput("; ");
    auditCheck(RAM_BANK == savedBank && (testStatus() & 4), "priority population changed caller bank or IRQ state");
    if (!savedIrqDisabled) testEnableInterrupts();
    auditCheck(elapsed <= PRIORITY_POPULATE_CYCLE_LIMIT, "PRIORITY TABLE POPULATION EXCEEDED ITS PERFORMANCE CEILING");
}
static void priority20AuditCasesBanked(void)
{
    View view;
    Loop loop;
    Cel cel;
    ViewTable table;
    ViewTableMetadata metadata;
    VeraSpriteAddress addresses[4];
    byte rleRow[4] = {0x1f, 0x1f, 0x13, 0};
    byte updates[11], i, row;
    unsigned long second;
    memset(&view,0,sizeof(view)); memset(&loop,0,sizeof(loop));
    memset(&cel,0,sizeof(cel)); memset(&table,0,sizeof(table));
    b8SetupLineTables();
    view.loaded=TRUE; view.numberOfLoops=1; view.maxCels=1; view.maxVeraSlots=4;
    view.loops=(Loop*)b10BankedAlloc(sizeof(loop),&view.loopsBank);
    view.codeBlock=b10BankedAlloc(sizeof(rleRow) * 65,&view.codeBlockBank);
    for (row=0; row<65; ++row)
        memCpyBanked(view.codeBlock + row * sizeof(rleRow),rleRow,view.codeBlockBank,sizeof(rleRow));
    loop.numberOfCels=1; loop.cels=(Cel*)b10BankedAlloc(sizeof(cel),&loop.celsBank);
    loop.allocationWidth=SPR_ATTR_64; loop.allocationHeight=SPR_ATTR_64;
    cel.width=33; cel.height=65; cel.veraSlotsWidth=2; cel.veraSlotsHeight=2; cel.splitSegments=4;
    cel.bmp=view.codeBlock; cel.bitmapBank=view.codeBlockBank;
    setLoadedCel(&loop,&cel,0); setLoadedLoop(&view,&loop,0); setLoadedView(&view,247);
    table.currentView=247; table.xPos=10; table.yPos=80; table.priority=5; table.staleCounter=0;
    coldBulkSplit(&view,&loop,&cel,&table,FALSE);
    coldBulkSplit(&view,&loop,&cel,&table,TRUE);
    getLoadedCel(&loop,&cel,0); cel.flipped=FALSE; setLoadedCel(&loop,&cel,0);
    table.staleCounter=1; setViewTab(&table,0);
    bEResetViewTableMetadata(); bEResetSpritePointers(); bEResetSpritesUpdatedBuffer();
    bESetViewMetadata(&view,&table,247,0,0);
    memCpyBanked((byte*)&metadata,(byte*)viewTableMetadata,SPRITE_METADATA_BANK,sizeof(metadata));
    bDResetSpriteMemoryManager();
    for(i=0;i<4;++i) addresses[i]=bDFindFreeVramBlock(SPR_SIZE_64,SPR_SIZE_64);
    memCpyBanked((byte*)metadata.veraAddresses,(byte*)addresses,metadata.viewTableMetadataBank,sizeof(addresses));
    /* Left segment is hidden behind priority15; the last pixel belongs to
     * the right segment at x42 and must appear over priority4 on both row bands. */
    for(row=16;row<=80;++row) {
        for(i=0;i<80;++i) testPoke(0x44,PRIORITY_START+80UL*row+i);
        for(i=5;i<21;++i) testPoke(0xff,PRIORITY_START+80UL*row+i);
    }
    testDisableInterrupts();
    auditCheck(agiBlit(0,FALSE),"animated split priority fixture rejected valid cel");
    memCpyBanked(updates,bESpritesUpdatedBuffer,SPRITE_UPDATED_BANK,sizeof(updates));
    second=((unsigned long)updates[7]|((unsigned long)updates[8]<<8))*32;
    auditCheck(testPeek(second)==0x11,"ANIMATED SPLIT COLUMN READ PRIORITY FROM THE OBJECT ORIGIN");
    second=((unsigned long)updates[0]|((unsigned long)updates[1]<<8))*32;
    auditCheck(testPeek(second)==0,"animated split first column ignored foreground priority");
    testEnableInterrupts();
    getLoadedCel(&loop,&cel,0); cel.flipped=TRUE; setLoadedCel(&loop,&cel,0);
    for(row=16;row<=80;++row) {
        for(i=0;i<80;++i) testPoke(0x44,PRIORITY_START+80UL*row+i);
        testPoke(0xf4,PRIORITY_START+80UL*row+5);
    }
    bEResetSpritesUpdatedBuffer();
    testDisableInterrupts();
    auditCheck(agiBlit(0,FALSE),"mirrored split fixture rejected valid cel");
    memCpyBanked(updates,bESpritesUpdatedBuffer,SPRITE_UPDATED_BANK,sizeof(updates));
    auditCheck(updates[2]==22 && updates[3]==0,"MIRRORED SPLIT FULL COLUMN WAS PLACED USING THE WHOLE CEL WIDTH");
    auditCheck(updates[9]==214 && updates[10]==255,"mirrored split partial column lost its leading hardware padding");
    second=((unsigned long)updates[7]|((unsigned long)updates[8]<<8))*32;
    auditCheck(!testPeek(second),"mirrored partial column sampled priority outside its screen pixel");
    second=((unsigned long)updates[0]|((unsigned long)updates[1]<<8))*32;
    auditCheck(testPeek(second)==0x11 && testPeek(second+31)==0x11,"mirrored full column sampled beyond its screen span");
    testEnableInterrupts();
    bEResetSpritesUpdatedBuffer(); bEResetViewTableMetadata(); b9DiscardView(247); bDResetSpriteMemoryManager();
    castleFlagViewCase();
    priorityPopulationCase();
    auditOutput("PRIORITY20 AUDIT PASS\n");
}
void priority20AuditCases(void)
{
    priority20AuditCasesBanked();
}
