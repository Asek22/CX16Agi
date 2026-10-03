#include "audit.h"
#include "../../helpers.h"
#include "../../memoryManager.h"
#include <string.h>

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
    auditOutput("RESOURCE OWNERSHIP CASES; ");
    allocationOwnershipCase();
}
