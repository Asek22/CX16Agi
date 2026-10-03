#include "audit.h"
#include "../../paletteManager.h"
#include <string.h>
extern int allocatedPaletteOwners[];
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
    auditOutput("RENDER20 AUDIT PASS\n");
}
