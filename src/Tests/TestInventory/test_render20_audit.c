#include "audit.h"
#include "../../paletteManager.h"
#include "../../spriteAllocator.h"
#include "../../picture.h"
#include "../../view.h"
#include "../../loadingScreen.h"
#include <string.h>
extern int allocatedPaletteOwners[];
static byte rowBytes[65];
extern byte patCode, patNum, picColour;
extern boolean picDrawEnabled, priDrawEnabled;
#pragma wrapped-call (push, trampoline, SPRITE_UPDATED_BANK)
extern void bESplitCel(Cel* cel);
extern void bECellToVeraBulk(byte width, byte height, byte count, byte slots, byte x, byte y, byte priority);
#pragma wrapped-call (pop)
extern byte bEToBlitCelArray[], bEBulkAllocatedAddresses[];
#pragma wrapped-call (push, trampoline, SPRITE_UPDATED_BANK)
extern boolean bEAllocateSpriteMemoryBulk(SpriteAllocationSize width, SpriteAllocationSize height, byte count);
#pragma wrapped-call (pop)
static void denseSplitCase(void)
{
    byte row, bank;
    Cel cel;
    memset(&cel,0,sizeof(cel));
    memset(rowBytes,0x71,64); rowBytes[64]=0;
    cel.bmp=b10BankedAlloc(65*64,&bank); cel.bitmapBank=bank;
    cel.width=64; cel.height=64;
    for(row=0; row<64; ++row)
        memCpyBanked(cel.bmp+(unsigned)row*65,rowBytes,bank,65);
    bESplitCel(&cel);
    auditCheck(!cel.splitCelPointers && !cel.splitSegments,"dense split cel overflowed its segment workspace instead of failing safely");
    auditCheck(b10BankedDealloc(cel.bmp,bank),"dense cel source cleanup");
}
static void splitCase(byte width, byte height)
{
    byte bank, row, remaining, amount, segments, i;
    unsigned length, rowLength;
    unsigned pointers[16];
    Cel cel;
    memset(&cel,0,sizeof(cel));
    rowLength=((unsigned)width+14)/15+1;
    cel.bmp=b10BankedAlloc(rowLength*height,&bank); cel.bitmapBank=bank;
    cel.width=width; cel.height=height;
    for(row=0; row<height; ++row) {
        length=0;
        remaining=width;
        while(remaining) {
            amount=remaining>15?15:remaining;
            rowBytes[length++]=(byte)(((row/64+1)<<4)|amount);
            remaining-=amount;
        }
        rowBytes[length++]=0;
        memCpyBanked(cel.bmp+(unsigned)row*rowLength,rowBytes,bank,length);
    }
    bESplitCel(&cel);
    segments=((unsigned)width*2+63)/64*((unsigned)height+63)/64;
    auditCheck(cel.splitSegments==segments,"split cel computed the wrong segment count");
    memCpyBanked((byte*)pointers,(byte*)cel.splitCelPointers,cel.splitCelBank,segments*2);
    for(i=0; i<segments; ++i) {
        byte first;
        memCpyBanked(&first,(byte*)pointers[i],cel.splitCelBank,1);
        auditCheck((first>>4)==i/((width*2+63)/64)+1,"split cel row groups were written through wrong segment pointers");
    }
    auditCheck(b10BankedDealloc((byte*)cel.splitCelPointers,cel.splitCelBank),"split cel result cleanup");
    auditCheck(b10BankedDealloc(cel.bmp,bank),"split cel source cleanup");
}
#pragma wrapped-call (push, trampoline, PICTURE_CODE_OVERFLOW_BANK)
extern void b8PlotPattern(byte x, byte y);
#pragma wrapped-call (pop)
#pragma wrapped-call (push, trampoline, LINE_DRAW_BANK)
extern void b8SetupLineTables(void);
#pragma wrapped-call (pop)
static void brushCases(void)
{
    unsigned i;
    b8SetupLineTables();
    picDrawEnabled=TRUE; priDrawEnabled=FALSE; picColour=5;
    patCode=0x11; patNum=0;
    for(i=0; i<160; ++i) {
        testPoke(0xa5,STARTING_BYTE+166UL*160+i);
        testPoke(0xa5,STARTING_BYTE+167UL*160+i);
        testPoke(0xa5,STARTING_BYTE+168UL*160+i);
    }
    b8PlotPattern(0,167);
    auditCheck(testPeek(STARTING_BYTE+166UL*160)==0x55 && testPeek(STARTING_BYTE+167UL*160)==0x55,"left-edge picture brush disappeared after coordinate underflow");
    for(i=1; i<160; ++i)
        auditCheck(testPeek(STARTING_BYTE+167UL*160+i)==0xa5,"left-edge brush escaped its clipped rectangle");
    for(i=0; i<160; ++i)
        auditCheck(testPeek(STARTING_BYTE+168UL*160+i)==0xa5,"bottom-edge brush wrote beyond the picture rows");
    b8PlotPattern(159,167);
    auditCheck(testPeek(STARTING_BYTE+167UL*160+158)==0x55 && testPeek(STARTING_BYTE+167UL*160+159)==0x55,"right-edge picture brush lost valid pixels");
    for(i=0; i<160; ++i)
        auditCheck(testPeek(STARTING_BYTE+168UL*160+i)==0xa5,"right-edge brush wrote beyond the picture rows");
    picDrawEnabled=FALSE;
}
static void loaderCases(void)
{
    byte i;
    for(i=0; i<32; ++i) testPoke((byte)(i+0x30),STARTING_BYTE+(unsigned long)i);
    b6DisplayLoadingScreen();
    auditCheck(loadingScreenDisplayed,"loader did not publish its visible state");
    b6DisplayLoadingScreen();
    b6DismissLoadingScreen();
    auditCheck(!loadingScreenDisplayed,"loader did not retire its visible state");
    b6DismissLoadingScreen();
    for(i=0; i<32; ++i)
        auditCheck(testPeek(STARTING_BYTE+(unsigned long)i)==(byte)(i+0x30),"loader lifecycle overwrote the game picture");
}
static void spriteSpanCases(void)
{
    byte widths[4]={8,16,32,64};
    byte w,h;
    unsigned count, i, blocks;
    unsigned long address;
    for(w=0; w<4; ++w) for(h=0; h<4; ++h) {
        blocks=(unsigned)(widths[w]/8)*(widths[h]/8);
        count=TOTAL_REAL_BLOCKS/blocks;
        bDResetSpriteMemoryManager();
        for(i=0; i<count; ++i) {
            address=bDFindFreeVramBlock((SpriteAllocationSize)widths[w],(SpriteAllocationSize)widths[h]);
            auditCheck(address==VRAM_START+(unsigned long)i*blocks*32,"sprite allocation returned an overlapping or out-of-range span");
            auditCheck(address+(unsigned long)blocks*32<=VRAM_START+VRAM_SIZE,"sprite span crossed into palette VRAM");
        }
        auditCheck(!bDFindFreeVramBlock((SpriteAllocationSize)widths[w],(SpriteAllocationSize)widths[h]),"full sprite allocator crossed its terminal block");
        address=VRAM_START+(unsigned long)(count/2)*blocks*32;
        bDDeleteAllocation(address,(SpriteAllocationSize)widths[w],(SpriteAllocationSize)widths[h]);
        auditCheck(bDFindFreeVramBlock((SpriteAllocationSize)widths[w],(SpriteAllocationSize)widths[h])==address,"sprite free did not release the exact span for reuse");
    }
    bDResetSpriteMemoryManager();
}
static void paletteCases(void)
{
    PaletteGetResult result;
    byte i, slot, bank = RAM_BANK;
    int owners[NO_MANAGED_PALETTES];
    bFInitPaletteManager();
    for (i=0; i<NO_MANAGED_PALETTES; ++i) {
        slot=bFGetPalette(300+i, &result);
        auditCheck(slot==BASE_MANAGED_PALETTE+i && result==Allocated,"palette setup did not allocate distinct owners");
    }
    for (i=0; i<NO_MANAGED_PALETTES; ++i) {
        slot=bFGetPalette(300+i, &result);
        auditCheck(slot==BASE_MANAGED_PALETTE+i && result==AlreadyAllocated,"full palette pool rejected an existing owner");
    }
    slot=bFGetPalette(999, &result);
    auditCheck(slot==0 && result==FailToAllocate,"full palette pool admitted a new owner");
    auditCheck(RAM_BANK==bank,"palette lookup changed caller bank");
    bFInitPaletteManager();
    memCpyBanked((byte*)owners,(byte*)allocatedPaletteOwners,PALETTE_MANAGER_BANK,sizeof(owners));
    for (i=0; i<NO_MANAGED_PALETTES; ++i)
        auditCheck(owners[i]==0,"palette reset retained old owner words");
}
void render20AuditCases(void)
{
    paletteCases();
    spriteSpanCases();
    brushCases();
    auditOutput("SPLIT33X1; ");
    splitCase(33,1);
    auditOutput("SPLIT33X65; ");
    splitCase(33,65);
    auditOutput("SPLIT32X65; ");
    splitCase(32,65);
    auditOutput("SPLIT1X193; ");
    splitCase(1,193);
    auditOutput("SPLIT128X1; ");
    splitCase(128,1);
    auditOutput("SPLIT160X1; ");
    splitCase(160,1);
    auditOutput("SPLIT255X1; ");
    splitCase(255,1);
    auditOutput("DENSE SPLIT; ");
    denseSplitCase();
    loaderCases();
    auditOutput("RENDER20 AUDIT PASS\n");
}
