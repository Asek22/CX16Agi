#include "showObj.h"
#include "inventory.h"
#include "textLayer.h"
#include <cbm.h>
#include <string.h>

extern unsigned long inventoryAddress;
extern byte inventoryBuffer[], inventoryLength, inventoryWrite;
extern void inventoryTransfer(void);
extern byte inventoryMouseButtons(void);
extern byte b3LastBoxLines, b3LastBoxStartLine, b3PaletteRows, b3PaletteNumber;
extern long b3PaletteAddress;
extern char* currentTextBuffer;
extern unsigned long displayTextAddressToCopyTo;
#ifdef INVENTORY_TEST_SHOWOBJ
extern byte showObjTestSavedRows, showObjTestSavedBank;
#endif

static void overlayTransfer(unsigned long address, byte length, byte write)
{
   inventoryAddress = address;
   inventoryLength = length;
   inventoryWrite = write;
   inventoryTransfer();
}
static void overlayFrame(void)
{
   volatile unsigned* counter = &vSyncCounter;
   unsigned before = *counter;
   while (*counter == before) {}
}
static byte overlayVideo(byte value, byte restore)
{
   byte control;
   byte previous;
   asm("php");
   asm("sei");
   control = VERA.control;
   VERA.control = control & 1;
   previous = VERA.display.video;
   VERA.display.video = restore ? value : previous | 0x60;
   VERA.control = control;
   asm("plp");
   return previous;
}
#pragma code-name (push, "BANKRAM11")
static void overlayTextContext(byte* state, byte restore)
{
   if (restore) {
      memCpyBanked(&b3LastBoxLines, state, TEXT_CODE_BANK, 2);
      /* irqAsm.s lays palette address, row count, and number adjacently. */
      memCpyBanked((byte*)&b3PaletteAddress, state + 2, TEXT_CODE_BANK, 6);
      memCpyBanked((byte*)&displayTextAddressToCopyTo, state + 8, TEXT_CODE_BANK, 4);
      memCpyBanked((byte*)&currentTextBuffer, state + 12, TEXT_CODE_BANK, 2);
   } else {
      memCpyBanked(state, &b3LastBoxLines, TEXT_CODE_BANK, 2);
      memCpyBanked(state + 2, (byte*)&b3PaletteAddress, TEXT_CODE_BANK, 6);
      memCpyBanked(state + 8, (byte*)&displayTextAddressToCopyTo, TEXT_CODE_BANK, 4);
      memCpyBanked(state + 12, (byte*)&currentTextBuffer, TEXT_CODE_BANK, 2);
   }
}

void b11ShowObj(byte objNum)
{
   View localView;
   Loop localLoop;
   Cel localCel;
   SpriteAttributeSize spriteAttributeWidth, spriteAttributeHeight;
   SpriteAllocationSize spriteAllocationWidth, spriteAllocationHeight;
   VeraSpriteAddress spriteAddress;
   byte savedAttributes[8], savedTextContext[14], *savedText = 0, textBank, firstTextRow, savedTextRows;
   byte previousActive, previousVideo, wasLoaded, buttons;
   unsigned shifted, selectedPixelWidth;
   getLoadedView(&localView, objNum);
   wasLoaded = localView.loaded;
   if (!wasLoaded && !b9TryLoadViewFile(objNum)) return;

   getLoadedView(&localView, objNum);
   getLoadedLoop(&localView, &localLoop, 0);
   getLoadedCel(&localLoop, &localCel, 0);

   selectedPixelWidth = (unsigned)localCel.width * 2;
   if (selectedPixelWidth > SPR_SIZE_64 || localCel.height > SPR_SIZE_64) {
      if (!wasLoaded) b9DiscardView(objNum);
      return;
   }
   spriteAttributeWidth = selectedPixelWidth > SPR_SIZE_32 ? SPR_ATTR_64 :
      selectedPixelWidth > SPR_SIZE_16 ? SPR_ATTR_32 :
      selectedPixelWidth > SPR_SIZE_8 ? SPR_ATTR_16 : SPR_ATTR_8;
   spriteAttributeHeight = localCel.height > SPR_SIZE_32 ? SPR_ATTR_64 :
      localCel.height > SPR_SIZE_16 ? SPR_ATTR_32 :
      localCel.height > SPR_SIZE_8 ? SPR_ATTR_16 : SPR_ATTR_8;
   spriteAllocationWidth = bEGetSpriteAllocateSize(spriteAttributeWidth);
   spriteAllocationHeight = bEGetSpriteAllocateSize(spriteAttributeHeight);
   spriteAddress = bDFindFreeVramBlock(spriteAllocationWidth, spriteAllocationHeight);

   if(spriteAddress)
   {
    previousActive = inventoryActive;
    inventoryActive = TRUE;
    overlayTransfer(0x1fff8UL, 8, FALSE);
    memcpy(savedAttributes, inventoryBuffer, 8);
    asm("sei");
    SET_VERA_ADDRESS_ZP(spriteAddress, VERA_ADDRESS, VERA_ADDRESS_HIGH);
    bEClearVeraSprite(spriteAttributeWidth, spriteAttributeHeight);
    REENABLE_INTERRUPTS();

    b9CelToVera(&localCel, localLoop.celsBank, spriteAddress, MAX_PRIORITY, spriteAllocationWidth / 2, 0, 0, MAX_PRIORITY);
    /* DISPLAY_TEXT still runs with the modal flag set; suppress menu/status IRQ painting. */
    if (localView.description) {
       overlayTextContext(savedTextContext, FALSE);
       if (!b3DisplayMessageBoxWithSaveUnder(localView.description, localView.codeBlockBank,
          14, AUTO_CALC_COLUMN, TEXTBOX_PALETTE_NUMBER, DEFAULT_BOX_WIDTH, TRUE,
          &savedText, &textBank, &firstTextRow, &savedTextRows)) {
          inventoryActive = previousActive;
          overlayTextContext(savedTextContext, TRUE);
          if (savedText) b10BankedDealloc(savedText, textBank);
          bDDeleteAllocation(spriteAddress, spriteAllocationWidth, spriteAllocationHeight);
          if (!wasLoaded) b9DiscardView(objNum);
          return;
       }
       inventoryActive = TRUE;
    }
    shifted = spriteAddress >> 5;
    inventoryBuffer[0] = shifted;
    inventoryBuffer[1] = shifted >> 8;
    shifted = PICTURE_WIDTH-localCel.width;
    inventoryBuffer[2] = shifted;
    inventoryBuffer[3] = shifted >> 8;
    inventoryBuffer[4] = PICTURE_HEIGHT/2+localCel.height/2;
    inventoryBuffer[5] = 0;
    inventoryBuffer[6] = 12;
    inventoryBuffer[7] = spriteAttributeHeight << 6 | spriteAttributeWidth << 4 | localLoop.palette;
    overlayTransfer(0x1fff8UL, 8, TRUE);
    previousVideo = overlayVideo(0, FALSE);
    buttons = inventoryMouseButtons();
    overlayFrame();
    overlayFrame();
    while (!cbm_k_getin()) {
       byte next = inventoryMouseButtons();
       if (next && !buttons) break;
       buttons = next;
    }
    memcpy(inventoryBuffer, savedAttributes, 8);
    overlayTransfer(0x1fff8UL, 8, TRUE);
    overlayFrame();
    bDDeleteAllocation(spriteAddress, spriteAllocationWidth, spriteAllocationHeight);
    if (savedText) {
#ifdef INVENTORY_TEST_SHOWOBJ
       showObjTestSavedRows = savedTextRows;
       showObjTestSavedBank = firstTextRow;
#endif
       b3CopyTextRows(savedText, textBank, firstTextRow, savedTextRows, TRUE);
       b10BankedDealloc(savedText, textBank);
    }
    if (localView.description) overlayTextContext(savedTextContext, TRUE);
    overlayVideo(previousVideo, TRUE);
    inventoryActive = previousActive;
   }
   if (!wasLoaded) b9DiscardView(objNum);
}

#pragma code-name (pop)
