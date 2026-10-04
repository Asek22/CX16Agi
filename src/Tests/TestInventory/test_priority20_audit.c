#include "audit.h"
#include "../../view.h"
#include <string.h>
#pragma wrapped-call (push, trampoline, SPRITE_METADATA_BANK)
extern void bESetViewMetadata(View* view, ViewTable* table, byte viewNum, byte tableNum, byte slot);
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

void priority20AuditCases(void)
{
    View view;
    Loop loop;
    Cel cel;
    ViewTable table;
    ViewTableMetadata metadata;
    VeraSpriteAddress addresses[4];
    byte rle[65 * 4], updates[30], i, row;
    unsigned long second;
    memset(&view,0,sizeof(view)); memset(&loop,0,sizeof(loop));
    memset(&cel,0,sizeof(cel)); memset(&table,0,sizeof(table));
    for (row = 0; row < 65; ++row) {
        rle[row * 4] = 0x1f; rle[row * 4 + 1] = 0x1f;
        rle[row * 4 + 2] = 0x13; rle[row * 4 + 3] = 0;
    }
    b8SetupLineTables();
    view.loaded=TRUE; view.numberOfLoops=1; view.maxCels=1; view.maxVeraSlots=4;
    view.loops=(Loop*)b10BankedAlloc(sizeof(loop),&view.loopsBank);
    view.codeBlock=b10BankedAlloc(sizeof(rle),&view.codeBlockBank);
    memCpyBanked(view.codeBlock,rle,view.codeBlockBank,sizeof(rle));
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
    auditOutput("PRIORITY20 AUDIT PASS\n");
}
