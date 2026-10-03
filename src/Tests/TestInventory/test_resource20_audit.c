#include "audit.h"
#include "../../helpers.h"
#include "../../memoryManager.h"
#include "../../view.h"
#include <string.h>

#pragma wrapped-call (push, trampoline, SPRITE_METADATA_BANK)
extern void bESetViewMetadata(View* view, ViewTable* table, byte viewNum, byte tableNum, byte slot);
extern void bEResetViewTableMetadata(void);
#pragma wrapped-call (pop)

static void metadataStrideCase(void)
{
    View view;
    ViewTable table;
    ViewTableMetadata metadata;
    VeraSpriteAddress* pointers[2];
    memset(&view, 0, sizeof(view));
    memset(&table, 0, sizeof(table));
    view.numberOfLoops = 2;
    view.maxVeraSlots = 4;
    view.maxCels = 64;
    bEResetViewTableMetadata();
    bESetViewMetadata(&view, &table, 250, 0, 0);
    memCpyBanked((byte*)&metadata, (byte*)viewTableMetadata, SPRITE_METADATA_BANK, sizeof(metadata));
    memCpyBanked((byte*)pointers, (byte*)metadata.loopsVeraAddressesPointers, metadata.viewTableMetadataBank, sizeof(pointers));
    auditCheck(pointers[1] - pointers[0] == 256, "split cel metadata stride wrapped at 256 addresses");
    auditCheck(metadata.backBuffers - metadata.veraAddresses == 512, "split cel metadata backbuffer overlaps cel addresses");
    bEResetViewTableMetadata();
}

static void allocationOwnershipCase(void)
{
    byte bank, sentinel = 0xe7;
    byte* block = b10BankedAlloc(100, &bank);
    auditCheck(!b10BankedDealloc(block + 1, bank), "interior pointer freed its owning allocation");
    auditCheck(b10BankedDealloc(block, bank), "interior free lost the original allocation");
    auditCheck(!b10BankedDealloc((byte*)0x9fff, bank), "low RAM pointer accepted as banked allocation");
    auditCheck(!b10BankedDealloc((byte*)0xbf40, bank), "unused bank tail accepted as allocation");
    auditCheck(!b10BankedDealloc((byte*)0xa000, LARGE_FIRST_BANK + LARGE_NO_BANKS), "unowned bank accepted as allocation");
    auditCheck(!b10TryBankedAlloc(0, &sentinel) && sentinel == 0xe7, "zero byte allocation consumed a segment");
    auditCheck(!b10TryBankedAlloc(-1, &sentinel) && sentinel == 0xe7, "negative allocation consumed a segment");
}

void resource20AuditCases(void)
{
    char formatted[40];
    int length;
    byte callerBank;
    auditOutput("RESOURCE OWNERSHIP CASES; ");
    allocationOwnershipCase();
    callerBank = RAM_BANK;
    length = sprintfBanked(formatted, TINY_FIRST_BANK, "%u:%s:%u:%02x", 0u, "bank", 321u, 0xafu);
    auditCheck(!strcmp(formatted, "0:bank:321:af"), "banked format ignored format string or variadic arguments");
    auditCheck(length == 13, "banked format returned wrong output length");
    auditCheck(RAM_BANK == callerBank, "banked format changed caller bank");
    metadataStrideCase();
}
