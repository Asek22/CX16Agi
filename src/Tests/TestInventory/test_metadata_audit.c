#include "audit.h"
#include "../../view.h"
#include <string.h>

#pragma wrapped-call (push, trampoline, SPRITE_METADATA_BANK)
extern void bEResetViewTableMetadata(void);
extern void bESwitchMetadata(ViewTable* table, View* view, byte viewNum, byte entryNum);
#pragma wrapped-call (pop)

extern byte allocationArray[];
static byte before[ALLOCATION_ARRAY_SIZE], after[ALLOCATION_ARRAY_SIZE];
static void wrongBankRelease(void)
{
    ViewTableMetadata owner, cached;
    byte *list, *payload, *unrelated;
    byte listBank, payloadBank, unrelatedBank;
    memset(&owner, 0, sizeof(owner));
    memset(&cached, 0, sizeof(cached));
    memCpyBanked(before, allocationArray, MEMORY_MANAGEMENT_BANK, sizeof(before));
    list = b10BankedAlloc(10*sizeof(cached), &listBank);
    payload = b10BankedAlloc(1, &payloadBank);
    unrelated = b10BankedAlloc(4000, &unrelatedBank);
    auditCheck(listBank != payloadBank && payloadBank != unrelatedBank, "metadata fixture must use distinct banks");
    memsetBanked(list, 0, 10*sizeof(cached), listBank);
    cached.loopsVeraAddressesPointers = (VeraSpriteAddress**)payload;
    cached.viewTableMetadataBank = payloadBank;
    cached.inactiveBank = unrelatedBank;
    memCpyBanked(list, (byte*)&cached, listBank, sizeof(cached));
    owner.inactive = list;
    owner.inactiveBank = listBank;
    memCpyBanked((byte*)viewTableMetadata, (byte*)&owner, SPRITE_METADATA_BANK, sizeof(owner));
    bEResetViewTableMetadata();
    auditCheck(b10BankedDealloc(unrelated, unrelatedBank), "metadata cleanup freed unrelated bank allocation");
    memCpyBanked(after, allocationArray, MEMORY_MANAGEMENT_BANK, sizeof(after));
    auditCheck(!memcmp(before, after, sizeof(before)), "metadata cleanup leaked cached payload in its owning bank");
}
static void fullCacheSwitch(void)
{
    ViewTableMetadata owner, cached;
    View view;
    ViewTable table;
    byte *list, *payload;
    byte listBank, payloadBank, i;
    memset(&owner, 0, sizeof(owner));
    memset(&cached, 0, sizeof(cached));
    memset(&view, 0, sizeof(view));
    memset(&table, 0, sizeof(table));
    memCpyBanked(before, allocationArray, MEMORY_MANAGEMENT_BANK, sizeof(before));
    list = b10BankedAlloc(10*sizeof(cached), &listBank);
    owner.inactive = list;
    owner.inactiveBank = listBank;
    owner.viewNum = 10;
    owner.loopsVeraAddressesPointers = (VeraSpriteAddress**)b10BankedAlloc(1, &owner.viewTableMetadataBank);
    for (i=0; i<10; ++i) {
        payload = b10BankedAlloc(1, &payloadBank);
        cached.loopsVeraAddressesPointers = (VeraSpriteAddress**)payload;
        cached.viewTableMetadataBank = payloadBank;
        cached.viewNum = i;
        memCpyBanked(list+i*sizeof(cached), (byte*)&cached, listBank, sizeof(cached));
    }
    memCpyBanked((byte*)viewTableMetadata, (byte*)&owner, SPRITE_METADATA_BANK, sizeof(owner));
    view.numberOfLoops = view.maxCels = view.maxVeraSlots = 1;
    auditOutput("FULL METADATA CACHE SWITCH MUST RETURN; ");
    bESwitchMetadata(&table, &view, 11, 0);
    memCpyBanked((byte*)&owner, (byte*)viewTableMetadata, SPRITE_METADATA_BANK, sizeof(owner));
    auditCheck(owner.viewNum == 11 && owner.inactive && owner.loopsVeraAddressesPointers, "full metadata cache switch lost active ownership");
    bEResetViewTableMetadata();
    memCpyBanked(after, allocationArray, MEMORY_MANAGEMENT_BANK, sizeof(after));
    auditCheck(!memcmp(before, after, sizeof(before)), "full metadata cache switch leaked allocations");
}
void metadataAuditCases(void)
{
    auditOutput("METADATA LIFECYCLE CASES; ");
    wrongBankRelease();
    fullCacheSwitch();
}
