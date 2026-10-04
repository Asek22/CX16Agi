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
extern byte bEBulkAllocatedAddresses[];
extern byte bDSpriteAllocTable[];
extern void testDisableInterrupts(void), testEnableInterrupts(void);
#pragma wrapped-call (push, trampoline, CONTROLLER_BANK)
void celEightAddressCase(void);
void exhaustedAllocationCase(void);
#pragma wrapped-call (pop)

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

#pragma code-name (push, "BANKRAM01")
void celEightAddressCase(void)
{
    View view;
    Loop loop;
    Cel cel;
    ViewTable table;
    ViewTableMetadata metadata;
    VeraSpriteAddress first[8], target[8];
    byte rle[]={0x18,0}, row, i;
    memset(&view,0,sizeof(view)); memset(&loop,0,sizeof(loop));
    memset(&cel,0,sizeof(cel)); memset(&table,0,sizeof(table));
    view.loaded=TRUE; view.numberOfLoops=1; view.maxCels=9; view.maxVeraSlots=8;
    view.loops=(Loop*)b10BankedAlloc(sizeof(loop),&view.loopsBank);
    view.codeBlock=b10BankedAlloc(sizeof(rle)*8,&view.codeBlockBank);
    for(row=0;row<8;++row) memCpyBanked(view.codeBlock+(unsigned)row*sizeof(rle),rle,view.codeBlockBank,sizeof(rle));
    loop.numberOfCels=9; loop.cels=(Cel*)b10BankedAlloc(9*sizeof(cel),&loop.celsBank);
    memsetBanked((byte*)loop.cels,0,9*sizeof(cel),loop.celsBank);
    loop.allocationWidth=SPR_ATTR_8; loop.allocationHeight=SPR_ATTR_8;
    cel.width=8; cel.height=8; cel.splitSegments=1; cel.veraSlotsWidth=1; cel.veraSlotsHeight=1;
    cel.bmp=view.codeBlock; cel.bitmapBank=view.codeBlockBank;
    setLoadedCel(&loop,&cel,8); setLoadedLoop(&view,&loop,0); setLoadedView(&view,246);
    table.currentView=246; table.currentCel=8; table.numberOfCels=9; table.xPos=10; table.yPos=80; table.priority=15;
    setViewTab(&table,0);
    bEResetViewTableMetadata(); bEResetSpritePointers(); bEResetSpritesUpdatedBuffer();
    bESetViewMetadata(&view,&table,246,0,0);
    memCpyBanked((byte*)&metadata,(byte*)viewTableMetadata,SPRITE_METADATA_BANK,sizeof(metadata));
    bDResetSpriteMemoryManager();
    testDisableInterrupts();
    auditCheck(agiBlit(0,FALSE),"cel8 one-slot fixture rejected valid cel");
    testEnableInterrupts();
    memCpyBanked((byte*)first,(byte*)metadata.veraAddresses,metadata.viewTableMetadataBank,sizeof(first));
    memCpyBanked((byte*)target,(byte*)(metadata.veraAddresses+64),metadata.viewTableMetadataBank,sizeof(target));
    for(i=0;i<8;++i) auditCheck(target[i]!=0,"cel8 address group was not populated beyond byte 255");
    for(i=0;i<8;++i) auditCheck(first[i]==0,"cel8 address copy overwrote the first cel group");
    bEResetSpritesUpdatedBuffer(); bEResetViewTableMetadata(); b9DiscardView(246); bDResetSpriteMemoryManager();
}

void exhaustedAllocationCase(void)
{
    View view;
    Loop loop;
    Cel cel;
    ViewTable table;
    ViewTableMetadata metadata;
    VeraSpriteAddress address;
    byte rle[]={0x18,0}, row, chunk[64], update[7];
    byte* before;
    byte* after;
    unsigned int offset, length, i;
    memset(&view,0,sizeof(view)); memset(&loop,0,sizeof(loop));
    memset(&cel,0,sizeof(cel)); memset(&table,0,sizeof(table));
    view.loaded=TRUE; view.numberOfLoops=1; view.maxCels=1; view.maxVeraSlots=1;
    view.loops=(Loop*)b10BankedAlloc(sizeof(loop),&view.loopsBank);
    view.codeBlock=b10BankedAlloc(sizeof(rle)*8,&view.codeBlockBank);
    for(row=0;row<8;++row) memCpyBanked(view.codeBlock+(unsigned)row*sizeof(rle),rle,view.codeBlockBank,sizeof(rle));
    loop.numberOfCels=1; loop.cels=(Cel*)b10BankedAlloc(sizeof(cel),&loop.celsBank);
    loop.allocationWidth=SPR_ATTR_8; loop.allocationHeight=SPR_ATTR_8;
    cel.width=8; cel.height=8; cel.splitSegments=1; cel.veraSlotsWidth=1; cel.veraSlotsHeight=1;
    cel.bmp=view.codeBlock; cel.bitmapBank=view.codeBlockBank;
    setLoadedCel(&loop,&cel,0); setLoadedLoop(&view,&loop,0); setLoadedView(&view,245);
    table.currentView=245; table.xPos=10; table.yPos=80; table.priority=15;
    setViewTab(&table,0);
    bEResetViewTableMetadata(); bEResetSpritePointers(); bEResetSpritesUpdatedBuffer();
    bESetViewMetadata(&view,&table,245,0,0);
    memCpyBanked((byte*)&metadata,(byte*)viewTableMetadata,SPRITE_METADATA_BANK,sizeof(metadata));

    bDResetSpriteMemoryManager();
    for(row=0;row<33;++row) auditCheck(bDFindFreeVramBlock(SPR_SIZE_64,SPR_SIZE_64)!=0,"test could not exhaust VRAM with 64x64 allocations");
    auditCheck(bDFindFreeVramBlock(SPR_SIZE_32,SPR_SIZE_64)!=0,"test could not exhaust the 32-block VRAM remainder");
    auditCheck(bDFindFreeVramBlock(SPR_SIZE_16,SPR_SIZE_64)!=0,"test could not exhaust the final 16 VRAM blocks");
    auditCheck(bDFindFreeVramBlock(SPR_SIZE_8,SPR_SIZE_8)==0,"test did not exhaust real VRAM");
    for(offset=0;offset<TOTAL_REAL_BLOCKS;offset+=64) {
        length=TOTAL_REAL_BLOCKS-offset;
        if(length>sizeof(chunk)) length=sizeof(chunk);
        memCpyBanked(chunk,bDSpriteAllocTable+offset,SPRITE_MEMORY_MANAGER_BANK,length);
        for(i=0;i<length;++i) auditCheck(chunk[i]==1,"VRAM exhaustion fixture left an allocator block free");
    }
    memsetBanked(bEBulkAllocatedAddresses,0,MAX_BULK_ALLOCATED_SIZE,SPRITE_UPDATED_BANK);
    memCpyBanked((byte*)&before,(byte*)&bESpritesUpdatedBufferPointer,SPRITE_UPDATED_BANK,sizeof(before));
    testDisableInterrupts();
    auditCheck(!agiBlit(0,FALSE),"agiBlit reported success after uncached cel allocation failed");
    testEnableInterrupts();
    memCpyBanked((byte*)&after,(byte*)&bESpritesUpdatedBufferPointer,SPRITE_UPDATED_BANK,sizeof(after));
    auditCheck(after==before,"failed uncached agiBlit published a sprite update");
    memCpyBanked(update,bESpritesUpdatedBuffer,SPRITE_UPDATED_BANK,sizeof(update));
    for(i=0;i<sizeof(update);++i) auditCheck(update[i]==0,"failed uncached agiBlit wrote a sprite update");
    memCpyBanked((byte*)&address,(byte*)metadata.veraAddresses,metadata.viewTableMetadataBank,sizeof(address));
    auditCheck(address==0,"failed uncached cel allocation populated cached metadata");
    for(offset=0;offset<TOTAL_REAL_BLOCKS;offset+=64) {
        length=TOTAL_REAL_BLOCKS-offset;
        if(length>sizeof(chunk)) length=sizeof(chunk);
        memCpyBanked(chunk,bDSpriteAllocTable+offset,SPRITE_MEMORY_MANAGER_BANK,length);
        for(i=0;i<length;++i) auditCheck(chunk[i]==1,"failed uncached cel allocation mutated VRAM ownership");
    }
    bEResetSpritesUpdatedBuffer(); bEResetViewTableMetadata(); b9DiscardView(245); bDResetSpriteMemoryManager();
}
#pragma code-name (pop)

void blit20AuditCases(void)
{
    auditOutput("ROOM SPLIT CALLER CASES; ");
    splitCallerCase(16);
    splitCallerCase(33);
    exhaustedAllocationCase();
    celEightAddressCase();
}
