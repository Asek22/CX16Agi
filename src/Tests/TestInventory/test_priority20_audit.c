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

void priority20AuditCases(void)
{
    View view;
    Loop loop;
    Cel cel;
    ViewTable table;
    ViewTableMetadata metadata;
    VeraSpriteAddress addresses[2];
    byte rle[]={0x1f,0x1f,0x13,0}, updates[14], i;
    unsigned long second;
    memset(&view,0,sizeof(view)); memset(&loop,0,sizeof(loop));
    memset(&cel,0,sizeof(cel)); memset(&table,0,sizeof(table));
    b8SetupLineTables();
    view.loaded=TRUE; view.numberOfLoops=1; view.maxCels=1; view.maxVeraSlots=2;
    view.loops=(Loop*)b10BankedAlloc(sizeof(loop),&view.loopsBank);
    view.codeBlock=b10BankedAlloc(sizeof(rle),&view.codeBlockBank);
    memCpyBanked(view.codeBlock,rle,view.codeBlockBank,sizeof(rle));
    loop.numberOfCels=1; loop.cels=(Cel*)b10BankedAlloc(sizeof(cel),&loop.celsBank);
    loop.allocationWidth=SPR_ATTR_64; loop.allocationHeight=SPR_ATTR_8;
    cel.width=33; cel.height=1; cel.veraSlotsWidth=2; cel.veraSlotsHeight=1; cel.splitSegments=2;
    cel.bmp=view.codeBlock; cel.bitmapBank=view.codeBlockBank;
    setLoadedCel(&loop,&cel,0); setLoadedLoop(&view,&loop,0); setLoadedView(&view,247);
    table.currentView=247; table.xPos=10; table.yPos=80; table.priority=5; table.staleCounter=1;
    setViewTab(&table,0);
    bEResetViewTableMetadata(); bEResetSpritePointers(); bEResetSpritesUpdatedBuffer();
    bESetViewMetadata(&view,&table,247,0,0);
    memCpyBanked((byte*)&metadata,(byte*)viewTableMetadata,SPRITE_METADATA_BANK,sizeof(metadata));
    bDResetSpriteMemoryManager();
    for(i=0;i<2;++i) addresses[i]=bDFindFreeVramBlock(SPR_SIZE_64,SPR_SIZE_8);
    memCpyBanked((byte*)metadata.veraAddresses,(byte*)addresses,metadata.viewTableMetadataBank,sizeof(addresses));
    /* Left segment is hidden behind priority15; the last pixel belongs to
     * the right segment at x42 and must appear over priority4. */
    for(i=0;i<80;++i) testPoke(0x44,PRIORITY_START+80UL*80+i);
    for(i=5;i<21;++i) testPoke(0xff,PRIORITY_START+80UL*80+i);
    testDisableInterrupts();
    auditCheck(agiBlit(0,FALSE),"animated split priority fixture rejected valid cel");
    memCpyBanked(updates,bESpritesUpdatedBuffer,SPRITE_UPDATED_BANK,sizeof(updates));
    second=((unsigned long)updates[7]|((unsigned long)updates[8]<<8))*32;
    auditCheck(testPeek(second)==0x11,"ANIMATED SPLIT COLUMN READ PRIORITY FROM THE OBJECT ORIGIN");
    second=((unsigned long)updates[0]|((unsigned long)updates[1]<<8))*32;
    auditCheck(testPeek(second)==0,"animated split first column ignored foreground priority");
    testEnableInterrupts();
    getLoadedCel(&loop,&cel,0); cel.flipped=TRUE; setLoadedCel(&loop,&cel,0);
    for(i=0;i<80;++i) testPoke(0x44,PRIORITY_START+80UL*80+i);
    testPoke(0xf4,PRIORITY_START+80UL*80+5);
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
