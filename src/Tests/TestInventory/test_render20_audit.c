#include "audit.h"
#include "../../paletteManager.h"
#include "../../spriteAllocator.h"
#include "../../picture.h"
#include "../../view.h"
#include "../../loadingScreen.h"
#include "../../textLayer.h"
#include <string.h>
extern int allocatedPaletteOwners[];
static byte rowBytes[65];
extern byte patCode, patNum, picColour;
extern boolean picDrawEnabled, priDrawEnabled;
static void textPaletteKeyCases(void);
#pragma wrapped-call (push, trampoline, SPRITE_UPDATED_BANK)
extern void bESplitCel(Cel* cel);
extern void bECellToVeraBulk(byte width, byte height, byte count, byte slots, byte x, byte y, byte priority);
#pragma wrapped-call (pop)
extern byte bEToBlitCelArray[], bEBulkAllocatedAddresses[];
#pragma wrapped-call (push, trampoline, SPRITE_UPDATED_BANK)
extern boolean bEAllocateSpriteMemoryBulk(SpriteAllocationSize width, SpriteAllocationSize height, byte count);
#pragma wrapped-call (pop)
#define RELATIVE_DRAW_CYCLE_BASELINE 18975UL
#pragma wrapped-call (push, trampoline, PALETTE_MANAGER_BANK)
static void denseSplitCase(void);
static void splitCase(byte width, byte height);
static void brushCases(void);
static void relativePictureCases(void);
static void loaderCases(void);
static void spriteSpanCases(void);
static void paletteCases(void);
#pragma wrapped-call (pop)

#pragma wrapped-call (push, trampoline, CONTROLLER_BANK)
static void addToPicTopClipCase(void);
#pragma wrapped-call (pop)

#pragma code-name (push, "BANKRAM0F")
#pragma rodata-name (push, "BANKRAM0F")
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
static unsigned long cycles(void)
{
    byte a=*(volatile byte*)0x9fb8, b=*(volatile byte*)0x9fb9;
    byte c=*(volatile byte*)0x9fba, d=*(volatile byte*)0x9fbb;
    return ((unsigned long)d<<24)|((unsigned long)c<<16)|((unsigned)b<<8)|a;
}
static void relativePictureCases(void)
{
    byte streamBytes[] = {0xf0,5,0xf7,10,80,0x10,0xf1,0xf0,5,0xf7,0,80,0x90,0xff};
    char measurement[64];
    PictureFile pictureFile, original;
    byte bank, x, edgePixel, validPixels, rowUnchanged, adjacentUnchanged, allocationReleased;
    byte* stream = b10BankedAlloc(sizeof(streamBytes), &bank);
    unsigned long start, elapsed;
    auditCheck(stream != 0, "relative picture stream allocation");
    memCpyBanked(stream, streamBytes, bank, sizeof(streamBytes));
    memCpyBanked((byte*)&original, (byte*)&loadedPictures[251], PICTURE_DATA_BANK, sizeof(original));
    memset(&pictureFile, 0, sizeof(pictureFile));
    pictureFile.loaded = TRUE;
    pictureFile.size = sizeof(streamBytes);
    pictureFile.data = stream;
    pictureFile.bank = bank;
    memCpyBanked((byte*)&loadedPictures[251], (byte*)&pictureFile, PICTURE_DATA_BANK, sizeof(pictureFile));
    for (x=0; x<160; ++x) testPoke(0xaa, STARTING_BYTE+80UL*160+x);
    for (x=0; x<160; ++x) testPoke(0xa5, STARTING_BYTE+81UL*160+x);
    b8SetupLineTables();
    start = cycles();
    b11DrawPic(stream, sizeof(streamBytes), FALSE, 251);
    elapsed = cycles() - start;
    sprintf(measurement, "RELATIVE DRAW CYCLES: %lu; BASELINE: %lu; ", elapsed, RELATIVE_DRAW_CYCLE_BASELINE);
    auditOutput(measurement);
    edgePixel = testPeek(STARTING_BYTE+80UL*160)==0x55;
    validPixels = testPeek(STARTING_BYTE+80UL*160+10)==0x55 && testPeek(STARTING_BYTE+80UL*160+11)==0x55;
    rowUnchanged = TRUE;
    adjacentUnchanged = TRUE;
    for (x=0; x<160; ++x) {
        if (x != 0 && x != 10 && x != 11)
            if (testPeek(STARTING_BYTE+80UL*160+x)!=0xaa) rowUnchanged = FALSE;
        if (testPeek(STARTING_BYTE+81UL*160+x)!=0xa5) adjacentUnchanged = FALSE;
    }
    memCpyBanked((byte*)&loadedPictures[251], (byte*)&original, PICTURE_DATA_BANK, sizeof(original));
    allocationReleased = b10BankedDealloc(stream, bank);
    auditCheck(edgePixel, "relative line lost the valid left-edge pixel");
    auditCheck(validPixels, "relative line changed valid in-bounds pixels");
    auditCheck(rowUnchanged, "relative line wrapped across the picture row");
    auditCheck(adjacentUnchanged, "relative line escaped into an adjacent picture row");
    auditCheck(allocationReleased, "relative picture fixture allocation was not released");
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
    textPaletteKeyCases();
    memCpyBanked((byte*)owners,(byte*)allocatedPaletteOwners,PALETTE_MANAGER_BANK,sizeof(owners));
    for (i=0; i<NO_MANAGED_PALETTES; ++i)
        auditCheck(owners[i]==0,"palette reset retained old owner words");
}
#pragma code-name (push, "CODE")
#pragma rodata-name (push, "CODE")
static void textPaletteKeyCases(void)
{
    byte savedSource[6], savedFirst[4], savedSecond[4];
    byte oldForeground, oldBackground;
    byte firstSlot, secondSlot, repeatedSlot, i;
    unsigned long firstAddress = PALETTE_START + BASE_MANAGED_PALETTE * 32UL;
    unsigned long secondAddress = firstAddress + 32;

    for (i=0; i<6; ++i) savedSource[i]=testPeek(PALETTE_START+i);
    for (i=0; i<4; ++i) {
        savedFirst[i]=testPeek(firstAddress+2+i);
        savedSecond[i]=testPeek(secondAddress+2+i);
    }
    memCpyBanked(&oldForeground, &b3CurrentForegroundColour, TEXT_CODE_BANK, 1);
    memCpyBanked(&oldBackground, &b3CurrentBackgroundColour, TEXT_CODE_BANK, 1);

    testPoke(0x11, PALETTE_START);     testPoke(0x02, PALETTE_START+1);
    testPoke(0x33, PALETTE_START+2);   testPoke(0x04, PALETTE_START+3);
    testPoke(0x55, PALETTE_START+4);   testPoke(0x06, PALETTE_START+5);
    bFInitPaletteManager();
    firstSlot=b3SetTextColor(1,1);
    secondSlot=b3SetTextColor(2,0);
    auditCheck(firstSlot==BASE_MANAGED_PALETTE &&
               secondSlot==BASE_MANAGED_PALETTE+1,
               "distinct text color pairs shared a palette owner");
    auditCheck(testPeek(firstAddress+2)==0x33 && testPeek(firstAddress+3)==0x04 &&
               testPeek(firstAddress+4)==0x33 && testPeek(firstAddress+5)==0x04,
               "first text palette did not receive its foreground and background colors");
    auditCheck(testPeek(secondAddress+2)==0x55 && testPeek(secondAddress+3)==0x06 &&
               testPeek(secondAddress+4)==0x11 && testPeek(secondAddress+5)==0x02,
               "second text palette did not receive its foreground and background colors");
    repeatedSlot=b3SetTextColor(1,1);
    auditCheck(repeatedSlot==firstSlot &&
               testPeek(firstAddress+2)==0x33 && testPeek(firstAddress+3)==0x04 &&
               testPeek(firstAddress+4)==0x33 && testPeek(firstAddress+5)==0x04,
               "text palette cache hit changed its previously published colors");

    for (i=0; i<6; ++i) testPoke(savedSource[i], PALETTE_START+i);
    for (i=0; i<4; ++i) {
        testPoke(savedFirst[i], firstAddress+2+i);
        testPoke(savedSecond[i], secondAddress+2+i);
    }
    memCpyBanked(&b3CurrentForegroundColour, &oldForeground, TEXT_CODE_BANK, 1);
    memCpyBanked(&b3CurrentBackgroundColour, &oldBackground, TEXT_CODE_BANK, 1);
    bFInitPaletteManager();
}
#pragma rodata-name (pop)
#pragma code-name (pop)
#pragma code-name (push, "BANKRAM01")
#pragma rodata-name (push, "BANKRAM01")
static void addToPicTopClipCase(void)
{
    static const byte resource[] = {0x12,0x34,0,17,0, 0,0,1,0,0,7,0, 1,3,0, 2,2,15,0x12,0,0x22,0};
    AGIFilePosType position, oldPosition;
    FILE* output;
    unsigned long address;
    byte i;
    byte savedBank = RAM_BANK;
    output = fopen("vol.8", "wb");
    auditCheck(output != 0, "add.to.pic clipped VIEW fixture creation");
    auditCheck(fwrite(resource, 1, sizeof(resource), output) == sizeof(resource), "add.to.pic clipped VIEW fixture write");
    fclose(output);
    memCpyBanked((byte*)&oldPosition, (byte*)&viewdir[248], DIRECTORY_BANK, sizeof(oldPosition));
    memset(&position, 0, sizeof(position)); position.fileNum = 8;
    memCpyBanked((byte*)&viewdir[248], (byte*)&position, DIRECTORY_BANK, sizeof(position));
    b9LoadViewFile(248);
    b8SetupLineTables();
    address = b8GetVeraPictureAddress(10, 0);
    for (i = 0; i < 3; ++i) {
        testPoke(0xa5, address - 161 + i);
        testPoke(0xa5, address - 1 + i);
    }
    testPoke(0xa5, address + 2);
    testPoke(0x44, PRIORITY_START + 4);
    testPoke(0x44, PRIORITY_START + 5);
    testPoke(0x44, PRIORITY_START + 6);
    b9AddToPic(248, 0, 0, 10, 0, 5, 15);
    auditCheck(testPeek(address) == 0x22 && testPeek(address + 1) == 0x22,
               "ADD.TO.PIC DREW THE HIDDEN TOP RLE ROW OR SKIPPED THE VISIBLE ROW");
    auditCheck(testPeek(address - 1) == 0xa5 && testPeek(address + 2) == 0xa5,
               "top-clipped add.to.pic changed horizontal neighbors");
    for (i = 0; i < 3; ++i)
        auditCheck(testPeek(address - 161 + i) == 0xa5, "top-clipped add.to.pic wrote into the offscreen row");
    auditCheck(testPeek(PRIORITY_START + 4) == 0x44 && testPeek(PRIORITY_START + 5) == 0x44 &&
               testPeek(PRIORITY_START + 6) == 0x44, "top-clipped add.to.pic changed priority pixels");
    b9DiscardView(248);
    memCpyBanked((byte*)&viewdir[248], (byte*)&oldPosition, DIRECTORY_BANK, sizeof(oldPosition));
    auditCheck(remove("vol.8") == 0, "add.to.pic clipped VIEW fixture cleanup");
    auditCheck(RAM_BANK == savedBank, "add.to.pic clipping changed caller bank");
}
#pragma rodata-name (pop)
#pragma code-name (pop)
#pragma rodata-name (pop)
#pragma code-name (pop)
void render20AuditCases(void)
{
    addToPicTopClipCase();
    paletteCases();
    spriteSpanCases();
    brushCases();
    relativePictureCases();
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
