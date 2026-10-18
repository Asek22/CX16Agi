#include "audit.h"
#include "../../agifiles.h"
#include "../../memoryManager.h"
#include "../../view.h"
#include <string.h>

extern byte allocationArray[];
#pragma wrapped-call (push, trampoline, 0x0f)
extern boolean b9ValidateViewRleForInventoryTest(AGIFile* resource);
#pragma wrapped-call (pop)

#define ALIAS_COUNT 255
#define VIEW_HEADER_SIZE (5 + ALIAS_COUNT * 2)
#define VIEW_RESOURCE_SIZE (VIEW_HEADER_SIZE + 8)

static byte allocationsBefore[ALLOCATION_ARRAY_SIZE];
static byte allocationsAfter[ALLOCATION_ARRAY_SIZE];

#pragma code-name (push, "BANKRAM08")
void viewaliasesAuditCases(void)
{
    AGIFile resource;
    byte bank, i, oneLoop = 1, loopCount = ALIAS_COUNT;
    byte pair[2], loopHeader[8] = {1, 3, 0, 1, 1, 0, 0x11, 0};
    byte singleLoopValid, aliasesValid, singleMapClean, aliasMapClean, resourceClean;

    memset(&resource, 0, sizeof(resource));
    resource.code = b10BankedAlloc(VIEW_RESOURCE_SIZE, &bank);
    auditCheck(resource.code != NULL, "maximum alias VIEW resource allocation failed");
    resource.codeBank = bank;
    resource.totalSize = VIEW_RESOURCE_SIZE;
    resource.codeSize = VIEW_RESOURCE_SIZE;
    memsetBanked(resource.code, 0, VIEW_RESOURCE_SIZE, bank);
    pair[0] = ALIAS_COUNT;
    memCpyBanked(resource.code + 2, pair, bank, 1);
    for (i = 0; i < ALIAS_COUNT; ++i) {
        pair[0] = (byte)VIEW_HEADER_SIZE;
        pair[1] = (byte)(VIEW_HEADER_SIZE >> 8);
        memCpyBanked(resource.code + 5 + (unsigned int)i * 2, pair, bank, 2);
    }
    memCpyBanked(resource.code + VIEW_HEADER_SIZE, loopHeader, bank, sizeof(loopHeader));
    memCpyBankedBetween(allocationsBefore, 0x01, allocationArray,
        MEMORY_MANAGEMENT_BANK, sizeof(allocationsBefore));

    memCpyBanked(resource.code + 2, &oneLoop, bank, 1);
    singleLoopValid = b9ValidateViewRleForInventoryTest(&resource);
    memCpyBankedBetween(allocationsAfter, 0x01, allocationArray,
        MEMORY_MANAGEMENT_BANK, sizeof(allocationsAfter));
    singleMapClean = !memcmp(allocationsBefore, allocationsAfter, sizeof(allocationsBefore));

    memCpyBanked(resource.code + 2, &loopCount, bank, 1);
    aliasesValid = b9ValidateViewRleForInventoryTest(&resource);
    memCpyBankedBetween(allocationsAfter, 0x01, allocationArray,
        MEMORY_MANAGEMENT_BANK, sizeof(allocationsAfter));
    aliasMapClean = !memcmp(allocationsBefore, allocationsAfter, sizeof(allocationsBefore));
    resourceClean = b10BankedDealloc(resource.code, bank);

    auditCheck(singleLoopValid, "RLE validator rejected a valid high-offset loop");
    auditCheck(singleMapClean, "single-loop RLE validation leaked its structure map");
    auditCheck(aliasesValid, "RLE validator rejected 255 aliases to one valid loop");
    auditCheck(aliasMapClean, "255-loop RLE validation leaked its structure map");
    auditCheck(resourceClean, "maximum alias VIEW resource cleanup failed");
    auditOutput("VIEW ALIAS RLE AUDIT PASS\n");
}
#pragma code-name (pop)
