#include "showObj.h"
#include "inventory.h"
#include <cbm.h>
#include <string.h>

extern unsigned long inventoryAddress;
extern byte inventoryBuffer[], inventoryLength, inventoryWrite;
extern void inventoryTransfer(void);
extern byte inventoryMouseButtons(void);

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
static void textOverlay(byte* saved, byte bank, byte restore)
{
   unsigned offset;
   for (offset = 0; offset < 4096; offset += 64) {
      if (restore) {
         memCpyBanked(inventoryBuffer, saved+offset, bank, 64);
         overlayTransfer(MAPBASE+(unsigned long)offset, 64, TRUE);
      } else {
         overlayTransfer(MAPBASE+(unsigned long)offset, 64, FALSE);
         memCpyBanked(saved+offset, inventoryBuffer, bank, 64);
      }
   }
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
void b11ShowObj(byte objNum)
{
   View localView;
   Loop localLoop;
   Cel localCel;
   SpriteAllocationSize spriteAllocationWidth, spriteAllocationHeight;
   VeraSpriteAddress spriteAddress;
   byte savedAttributes[8], *savedText, textBank;
   byte previousActive, previousVideo, wasLoaded, buttons;
   unsigned shifted;
   getLoadedView(&localView, objNum);
   wasLoaded = localView.loaded;
   b9LoadViewFile(objNum);

   getLoadedView(&localView, objNum);
   getLoadedLoop(&localView, &localLoop, 0);
   getLoadedCel(&localLoop, &localCel, 0);

   spriteAllocationWidth = bEGetSpriteAllocateSize(localLoop.allocationWidth);
   spriteAllocationHeight = bEGetSpriteAllocateSize(localLoop.allocationHeight);
   if (localCel.width > spriteAllocationWidth / 2 || localCel.height > spriteAllocationHeight) {
      if (!wasLoaded) b9DiscardView(objNum);
      return;
   }
   savedText = b10TryBankedAlloc(4096, &textBank);
   if (!savedText) {
      if (!wasLoaded) b9DiscardView(objNum);
      return;
   }
   spriteAddress = bDFindFreeVramBlock(spriteAllocationWidth, spriteAllocationHeight);

   if(spriteAddress)
   {
    previousActive = inventoryActive;
    inventoryActive = TRUE;
    overlayTransfer(0x1fff8UL, 8, FALSE);
    memcpy(savedAttributes, inventoryBuffer, 8);
    textOverlay(savedText, textBank, FALSE);
    asm("sei");
    SET_VERA_ADDRESS_ZP(spriteAddress, VERA_ADDRESS, VERA_ADDRESS_HIGH);
	 bEClearVeraSprite(localLoop.allocationWidth, localLoop.allocationHeight);
    REENABLE_INTERRUPTS();

    b9CelToVera(&localCel, localLoop.celsBank, spriteAddress, MAX_PRIORITY, spriteAllocationWidth / 2, 0, 0, MAX_PRIORITY);
    /* The text renderer uses IRQ commands; allow it to finish before freezing
     * the modal display. Slot publication itself uses the protected transfer. */
    if (localView.description) {
       inventoryActive = FALSE;
       b3DisplayMessageBox(localView.description, localView.codeBlockBank,14, AUTO_CALC_COLUMN, TEXTBOX_PALETTE_NUMBER, DEFAULT_BOX_WIDTH, TRUE);
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
    inventoryBuffer[7] = localLoop.allocationHeight << 6 | localLoop.allocationWidth << 4 | localLoop.palette;
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
    textOverlay(savedText, textBank, TRUE);
    overlayVideo(previousVideo, TRUE);
    inventoryActive = previousActive;
   }
   b10BankedDealloc(savedText, textBank);
   if (!wasLoaded) b9DiscardView(objNum);
}

#pragma code-name (pop)
