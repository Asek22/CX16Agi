#include "audit.h"
#include "../../view.h"
#include <string.h>
static byte rowBytes[3];
static unsigned long cycles(void)
{
    byte a=*(volatile byte*)0x9fb8, b=*(volatile byte*)0x9fb9;
    byte c=*(volatile byte*)0x9fba, d=*(volatile byte*)0x9fbb;
    return ((unsigned long)d<<24)|((unsigned long)c<<16)|((unsigned)b<<8)|a;
}
static void printCycles(unsigned long elapsed)
{
    byte i, digit;
    auditOutput("SPLIT UPLOAD CYCLES HEX: ");
    for(i=0; i<8; ++i) {
        digit=(byte)(elapsed>>28);
        *(volatile byte*)0x9fbb=(digit<10?'0'+digit:'A'+digit-10)&0x7f;
        elapsed<<=4;
    }
    auditOutput("; ");
}
extern byte bEToBlitCelArray[], bEBulkAllocatedAddresses[];
extern byte testStatus(void);
extern void testDisableInterrupts(void), testEnableInterrupts(void);
#pragma wrapped-call (push, trampoline, SPRITE_UPDATED_BANK)
extern void bESplitCel(Cel* cel);
extern void bECellToVeraBulk(byte width, byte height, byte count, byte slots, byte x, byte y, byte priority);
extern boolean bEAllocateSpriteMemoryBulk(SpriteAllocationSize width, SpriteAllocationSize height, byte count);
#pragma wrapped-call (pop)
#pragma wrapped-call (push, trampoline, LINE_DRAW_BANK)
extern void b8SetupLineTables(void);
#pragma wrapped-call (pop)
void upload20AuditCases(void)
{
    Cel cel;
    byte bank, row, callerBank, attrs[8], pass, irqStatus;
    unsigned i;
    unsigned long addresses[3];
    unsigned long start, elapsed;
    b8SetupLineTables();
    memset(&cel,0,sizeof(cel));
    cel.bmp=b10BankedAlloc(195,&bank); cel.bitmapBank=bank;
    cel.width=16; cel.height=65;
    for(row=0; row<65; ++row) {
        rowBytes[0]=(row==64?0x2f:0x1f);
        rowBytes[1]=(row==64?0x21:0x11);
        rowBytes[2]=0;
        memCpyBanked(cel.bmp+(unsigned)row*3,rowBytes,bank,3);
    }
    bESplitCel(&cel);
    auditCheck(cel.splitCelPointers!=0,"split upload source did not split");
    bDResetSpriteMemoryManager();
    auditCheck(bEAllocateSpriteMemoryBulk(SPR_SIZE_32,SPR_SIZE_64,3),"split upload VRAM allocation");
    memCpyBanked((byte*)addresses,bEBulkAllocatedAddresses,SPRITE_UPDATED_BANK,sizeof(addresses));
    addresses[1]=addresses[2];
    memCpyBanked(bEBulkAllocatedAddresses,(byte*)addresses,SPRITE_UPDATED_BANK,8);
    for(i=0; i<3328; ++i) testPoke(0xa5,addresses[0]+i);
    for(i=0; i<168*80; ++i) testPoke(0x44,PRIORITY_START+(unsigned long)i);
    memCpyBanked(bEToBlitCelArray,(byte*)&cel,SPRITE_UPDATED_BANK,sizeof(cel));
    for(row=0; row<8; ++row) attrs[row]=testPeek(0x1fff8UL+row);
    callerBank=RAM_BANK;
    irqStatus=testStatus()&4;
    auditOutput("SPLIT UPLOAD MUST RETURN; ");
    elapsed=0;
    for(pass=0; pass<8; ++pass) {
        start=cycles();
        bECellToVeraBulk(SPR_ATTR_32,SPR_ATTR_64,1,2,0,0,15);
        elapsed+=cycles()-start;
        auditCheck(testPeek(addresses[0])==0x11,"IRQ interrupted split upload and lost the first cel pixel");
        for(i=0; i<1024; ++i)
            auditCheck(testPeek(addresses[0]+i)==0x11,"IRQ interrupted split upload and changed an interior cel pixel");
    }
    elapsed/=8;
    printCycles(elapsed);
    /* Before bounding segment rows:106,823 cycles, with a live-span overwrite.
     * Retain the same IRQ-enabled16x65 workload and prevent a2x regression. */
    auditCheck(elapsed<213646UL,"split upload exceeded the retained performance baseline budget");
    auditCheck(RAM_BANK==callerBank,"split upload changed the caller's RAM bank");
    auditCheck((testStatus()&4)==irqStatus,"split upload changed the caller's IRQ mask");
    for(row=0; row<8; ++row)
        auditCheck(testPeek(0x1fff8UL+row)==attrs[row],"split upload published sprite attributes during transfer");
    auditCheck(testPeek(addresses[0])==0x11,"first split upload lost its cel pixels");
    auditCheck(testPeek(addresses[1])==0x22,"second split upload lost its cel pixels");
    for(i=0; i<1024; ++i)
        auditCheck(testPeek(addresses[0]+1024+i)==0xa5,"first split upload overwrote an intervening live VRAM allocation");
    for(i=16; i<1024; ++i)
        auditCheck(testPeek(addresses[1]+i)==0,"split upload read past its final row");
    for(i=0; i<256; ++i)
        auditCheck(testPeek(addresses[1]+1024+i)==0xa5,"split upload wrote past its allocated VRAM span");
    bDResetSpriteMemoryManager();
    auditCheck(b10BankedDealloc((byte*)cel.splitCelPointers,cel.splitCelBank),"split upload result cleanup");
    auditCheck(b10BankedDealloc(cel.bmp,bank),"split upload source cleanup");
}
