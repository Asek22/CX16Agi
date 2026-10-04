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

static void splitCallerCase(byte width)
{
    View view;
    Loop loop;
    Cel cel, returned;
    ViewTable table;
    ViewTableMetadata metadata;
    VeraSpriteAddress addresses[4];
    byte rle[4], updates[28], row, remaining=width, length=0, slots=((unsigned)width*2+63)/64*2;
    unsigned long begin, elapsed;
    memset(&view,0,sizeof(view)); memset(&loop,0,sizeof(loop));
    memset(&cel,0,sizeof(cel)); memset(&table,0,sizeof(table));
    while(remaining) {
        byte run=remaining>15 ? 15 : remaining;
        rle[length++]=0x10|run; remaining-=run;
    }
    rle[length++]=0;
    view.loaded=TRUE; view.numberOfLoops=1; view.maxCels=2; view.maxVeraSlots=slots;
    view.loops=(Loop*)b10BankedAlloc(sizeof(loop),&view.loopsBank);
    view.codeBlock=b10BankedAlloc((unsigned)length*65,&view.codeBlockBank);
    loop.numberOfCels=2; loop.cels=(Cel*)b10BankedAlloc(2*sizeof(cel),&loop.celsBank);
    loop.allocationWidth=width==16 ? SPR_ATTR_32 : SPR_ATTR_64; loop.allocationHeight=SPR_ATTR_64;
    cel.width=width; cel.height=65; cel.veraSlotsWidth=slots/2; cel.veraSlotsHeight=2; cel.splitSegments=slots;
    cel.bmp=view.codeBlock; cel.bitmapBank=view.codeBlockBank;
    for(row=0;row<65;++row) memCpyBanked(cel.bmp+(unsigned)row*length,rle,cel.bitmapBank,length);
    setLoadedCel(&loop,&cel,0); setLoadedCel(&loop,&cel,1);
    setLoadedLoop(&view,&loop,0); setLoadedView(&view,248);
    table.currentView=248; table.xPos=10; table.yPos=100; table.priority=15;
    setViewTab(&table,0);
    bEResetViewTableMetadata(); bEResetSpritePointers(); bEResetSpritesUpdatedBuffer();
    bESetViewMetadata(&view,&table,248,0,0);
    memCpyBanked((byte*)&metadata,(byte*)viewTableMetadata,SPRITE_METADATA_BANK,sizeof(metadata));
    bDResetSpriteMemoryManager();
    for(row=0;row<slots;++row) {
        addresses[row]=bDFindFreeVramBlock(width==16 ? SPR_SIZE_32 : SPR_SIZE_64,SPR_SIZE_64);
        auditCheck(addresses[row]!=0,"split caller fixture VRAM allocation");
    }
    memCpyBanked((byte*)metadata.veraAddresses,(byte*)addresses,metadata.viewTableMetadataBank,slots*sizeof(*addresses));
    testDisableInterrupts();
    begin=cycles();
    auditCheck(agiBlit(0,FALSE),"tall split caller rejected valid cached cel");
    elapsed=cycles()-begin;
    reportCycles(elapsed);
    /* Whole-loop splitting cost 226,335 cycles for this identical workload. */
    auditCheck(width!=16 || elapsed<180000UL,"room blitter regressed cold selected-cel rendering performance");
    memCpyBanked(updates,bESpritesUpdatedBuffer,SPRITE_UPDATED_BANK,slots*7);
    testEnableInterrupts();
    getLoadedCel(&loop,&returned,0);
    auditCheck(returned.splitCelPointers!=0,"tall narrow cel was never split by production blit");
    getLoadedCel(&loop,&cel,1);
    auditCheck(!cel.splitCelPointers,"room blitter allocated split art for an unselected cel");
    for(row=0;row<slots;++row) {
        auditCheck(updates[row*7+2]==20+(row%(slots/2))*64 && !updates[row*7+3],"split columns were placed at the wrong horizontal position");
        auditCheck(updates[row*7+4]==updates[4]+(row/(slots/2))*64,"split rows were placed at the wrong vertical position");
    }
    bEResetSpritesUpdatedBuffer();
    testDisableInterrupts();
    for(row=0;row<36/slots;++row) auditCheck(agiBlit(0,FALSE),"room update buffer rejected its valid 36-record boundary");
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
    splitCallerCase(16);
    splitCallerCase(33);
}
