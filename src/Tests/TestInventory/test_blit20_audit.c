#include "audit.h"
#include "../../view.h"
#include "../../spriteAllocator.h"
#include <string.h>
#pragma wrapped-call (push, trampoline, SPRITE_METADATA_BANK)
extern void bESetViewMetadata(View* view, ViewTable* table, byte viewNum, byte tableNum, byte slot);
extern void bEResetViewTableMetadata(void);
extern void bEResetSpritesUpdatedBuffer(void);
extern void bEResetSpritePointers(void);
#pragma wrapped-call (pop)
extern boolean agiBlit(byte entry, boolean disableInterrupts);
extern void setLoadedLoop(View* view, Loop* loop, byte number);
extern void setLoadedCel(Loop* loop, Cel* cel, byte number);
extern byte bESpritesUpdatedBuffer[];
extern byte* bESpritesUpdatedBufferPointer;
extern void testDisableInterrupts(void), testEnableInterrupts(void);

static unsigned long cycles(void)
{
    byte a=*(volatile byte*)0x9fb8, b=*(volatile byte*)0x9fb9;
    byte c=*(volatile byte*)0x9fba, d=*(volatile byte*)0x9fbb;
    return ((unsigned long)d<<24)|((unsigned long)c<<16)|((unsigned)b<<8)|a;
}
static void reportCycles(unsigned long value)
{
    char digits[11];
    byte count=0;
    auditOutput("COLD TWO-CEL ROOM BLIT CYCLES: ");
    do { digits[count++]=48+value%10; value/=10; } while(value);
    while(count) *(volatile byte*)0x9fbb=digits[--count];
    auditOutput("; ");
}

static void splitCallerCase(void)
{
    View view;
    Loop loop;
    Cel cel, returned;
    ViewTable table;
    ViewTableMetadata metadata;
    VeraSpriteAddress addresses[2];
    byte rle[3] = {0x1f,0x11,0}, updates[14], row;
    unsigned long begin, elapsed;
    memset(&view,0,sizeof(view)); memset(&loop,0,sizeof(loop));
    memset(&cel,0,sizeof(cel)); memset(&table,0,sizeof(table));
    view.loaded=TRUE; view.numberOfLoops=1; view.maxCels=2; view.maxVeraSlots=2;
    view.loops=(Loop*)b10BankedAlloc(sizeof(loop),&view.loopsBank);
    view.codeBlock=b10BankedAlloc(195,&view.codeBlockBank);
    loop.numberOfCels=2; loop.cels=(Cel*)b10BankedAlloc(2*sizeof(cel),&loop.celsBank);
    loop.allocationWidth=SPR_ATTR_32; loop.allocationHeight=SPR_ATTR_64;
    cel.width=16; cel.height=65; cel.veraSlotsWidth=1; cel.veraSlotsHeight=2; cel.splitSegments=2;
    cel.bmp=view.codeBlock; cel.bitmapBank=view.codeBlockBank;
    for(row=0;row<65;++row) memCpyBanked(cel.bmp+(unsigned)row*3,rle,cel.bitmapBank,sizeof(rle));
    setLoadedCel(&loop,&cel,0); setLoadedCel(&loop,&cel,1);
    setLoadedLoop(&view,&loop,0); setLoadedView(&view,248);
    table.currentView=248; table.xPos=10; table.yPos=100; table.priority=15;
    setViewTab(&table,0);
    bEResetViewTableMetadata(); bEResetSpritePointers(); bEResetSpritesUpdatedBuffer();
    bESetViewMetadata(&view,&table,248,0,0);
    memCpyBanked((byte*)&metadata,(byte*)viewTableMetadata,SPRITE_METADATA_BANK,sizeof(metadata));
    bDResetSpriteMemoryManager();
    addresses[0]=bDFindFreeVramBlock(SPR_SIZE_32,SPR_SIZE_64);
    addresses[1]=bDFindFreeVramBlock(SPR_SIZE_32,SPR_SIZE_64);
    auditCheck(addresses[0] && addresses[1],"split caller fixture VRAM allocation");
    memCpyBanked((byte*)metadata.veraAddresses,(byte*)addresses,metadata.viewTableMetadataBank,sizeof(addresses));
    testDisableInterrupts();
    begin=cycles();
    auditCheck(agiBlit(0,FALSE),"tall split caller rejected valid cached cel");
    elapsed=cycles()-begin;
    reportCycles(elapsed);
    /* Whole-loop splitting cost 226,335 cycles for this identical workload. */
    auditCheck(elapsed<180000UL,"room blitter regressed cold selected-cel rendering performance");
    memCpyBanked(updates,bESpritesUpdatedBuffer,SPRITE_UPDATED_BANK,sizeof(updates));
    testEnableInterrupts();
    getLoadedCel(&loop,&returned,0);
    auditCheck(returned.splitCelPointers!=0,"tall narrow cel was never split by production blit");
    getLoadedCel(&loop,&cel,1);
    auditCheck(!cel.splitCelPointers,"room blitter allocated split art for an unselected cel");
    auditCheck(updates[9]==updates[2] && updates[10]==updates[3],"vertical split moved its second segment horizontally");
    auditCheck(updates[11]==updates[4]+64,"vertical split left its second segment on the first row");
    bEResetSpritesUpdatedBuffer();
    testDisableInterrupts();
    for(row=0;row<18;++row) auditCheck(agiBlit(0,FALSE),"room update buffer rejected its valid 36-record boundary");
    auditCheck(!agiBlit(0,FALSE),"room update buffer accepted records beyond its physical capacity");
    {
        byte* pointer;
        memCpyBanked((byte*)&pointer,(byte*)&bESpritesUpdatedBufferPointer,SPRITE_UPDATED_BANK,sizeof(pointer));
        auditCheck(pointer==bESpritesUpdatedBuffer+252,"rejected room update changed its publication pointer");
    }
    testEnableInterrupts();
    bEResetSpritesUpdatedBuffer();
    cel.width=224; cel.height=1; cel.veraSlotsWidth=7; cel.veraSlotsHeight=1; cel.splitSegments=7;
    cel.splitCelPointers=0; cel.splitCelBank=0;
    setLoadedCel(&loop,&cel,0);
    testDisableInterrupts();
    auditCheck(!agiBlit(0,FALSE),"room blitter exceeded the object's reserved six sprite slots");
    testEnableInterrupts();
    /* Restore the owned split allocation before discarding the real view. */
    setLoadedCel(&loop,&returned,0);
    bEResetViewTableMetadata(); b9DiscardView(248); bDResetSpriteMemoryManager();
}

void blit20AuditCases(void)
{
    auditOutput("ROOM SPLIT CALLER CASES; ");
    splitCallerCase();
}
