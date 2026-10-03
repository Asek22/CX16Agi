#include "audit.h"
#include "../../paletteManager.h"
#include <string.h>
extern int allocatedPaletteOwners[];
#include "../../picture.h"
#include "../../spriteAllocator.h"
extern byte patCode, patNum, picColour;
extern boolean picDrawEnabled, priDrawEnabled;
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
    auditOutput("RENDER20 AUDIT PASS\n");
}
