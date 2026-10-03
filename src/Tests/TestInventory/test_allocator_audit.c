#include "audit.h"
#include "../../spriteAllocator.h"
#include <string.h>
extern byte bDSpriteAllocTable[];
extern byte bEBulkAllocatedAddresses[];
#pragma wrapped-call (push, trampoline, SPRITE_UPDATED_BANK)
extern boolean bEAllocateSpriteMemoryBulk(SpriteAllocationSize width, SpriteAllocationSize height, byte number);
#pragma wrapped-call (pop)
static byte addressBefore[256];
static void checkOwned(unsigned int count, const char* message)
{
    byte chunk[64];
    unsigned int offset, i, length;
    for (offset=0; offset<TOTAL_REAL_BLOCKS; offset+=64) {
        length = TOTAL_REAL_BLOCKS-offset;
        if (length>64) length=64;
        memCpyBanked(chunk, bDSpriteAllocTable+offset, SPRITE_MEMORY_MANAGER_BANK, length);
        for (i=0; i<length; ++i) auditCheck(chunk[i] == (offset+i<count ? 1 : 0), message);
    }
}
void allocatorAuditCases(void)
{
    byte i, bank;
    unsigned int offset;
    byte chunk[64];
    bDResetSpriteMemoryManager();
    for (i=0; i<32; ++i)
        auditCheck(bDFindFreeVramBlock(SPR_SIZE_64, SPR_SIZE_64) != 0, "allocator audit setup exhausted VRAM");
    bank = RAM_BANK;
    auditCheck(!bEAllocateSpriteMemoryBulk(SPR_SIZE_64, SPR_SIZE_64, 2), "exhausted bulk allocation unexpectedly succeeded");
    auditCheck(RAM_BANK == bank, "bulk failure changed incoming RAM bank");
    checkOwned(2048, "failed sprite bulk allocation leaked its successful prefix");
    bDResetSpriteMemoryManager();
    memCpyBanked(addressBefore, bEBulkAllocatedAddresses, SPRITE_UPDATED_BANK, sizeof(addressBefore));
    auditCheck(!bEAllocateSpriteMemoryBulk(SPR_SIZE_8, SPR_SIZE_8, 0), "zero sprite bulk count was accepted");
    checkOwned(0, "zero sprite bulk count consumed VRAM");
    for (offset=0; offset<256; offset+=64) {
        memCpyBanked(chunk, bEBulkAllocatedAddresses+offset, SPRITE_UPDATED_BANK, sizeof(chunk));
        auditCheck(!memcmp(addressBefore+offset, chunk, sizeof(chunk)), "zero sprite count overwrote address output");
    }
    auditCheck(!bEAllocateSpriteMemoryBulk(SPR_SIZE_8, SPR_SIZE_8, 65), "oversized sprite bulk count was accepted");
    checkOwned(0, "oversized sprite bulk count consumed VRAM");
    auditCheck(bEAllocateSpriteMemoryBulk(SPR_SIZE_8, SPR_SIZE_8, 64), "64-entry sprite address output rejected");
    checkOwned(64, "valid sprite bulk allocation occupied incorrect spans");
    bDResetSpriteMemoryManager();
    auditOutput("ALLOCATOR AUDIT PASS\n");
}
