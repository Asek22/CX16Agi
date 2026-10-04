#include "audit.h"
#include "../../debugHelper.h"
#include "../../memoryManager.h"
#include "../../helpers.h"

#include <stdio.h>
#include <string.h>

#pragma wrapped-call (push, trampoline, DEBUG_INIT_BANK)
extern unsigned int b5DebugFileRead(char* debugFileName, byte* ptrDebugBank);
#pragma wrapped-call (pop)

static byte fixtureByte(unsigned int offset)
{
    return (byte)(offset * 37u + 11u);
}

static void writeDebugFixture(const char* fileName, unsigned int length)
{
    byte bytes[64];
    unsigned int offset = 0, count, i;
    FILE* file = fopen(fileName, "wb");
    auditCheck(file != 0, "debug fixture creation");
    while (offset < length) {
        count = length - offset;
        if (count > sizeof(bytes)) count = sizeof(bytes);
        for (i = 0; i < count; ++i) bytes[i] = fixtureByte(offset + i);
        auditCheck(fwrite(bytes, 1, count, file) == count, "debug fixture write");
        offset += count;
    }
    auditCheck(fclose(file) == 0, "debug fixture close");
}

static void seedLargeAllocation(byte** pointer, byte* bank)
{
    byte canary = 0x5a;
    *pointer = b10BankedAlloc(LARGE_SIZE, bank);
    memsetBanked(*pointer, 0xa5, LARGE_SIZE, *bank);
    auditCheck((unsigned int)*pointer + LARGE_SIZE <= BANK_MAX,
        "debug allocation has no bank-local capacity sentinel");
    memCpyBanked(*pointer + LARGE_SIZE, &canary, *bank, 1);
    auditCheck(b10BankedDealloc(*pointer, *bank), "debug canary allocation release");
}

static void checkDebugBytes(byte* pointer, byte bank, unsigned int length)
{
    byte bytes[64];
    unsigned int offset, i, count;
    for (offset = 0; offset < LARGE_SIZE; offset += sizeof(bytes)) {
        count = LARGE_SIZE - offset;
        if (count > sizeof(bytes)) count = sizeof(bytes);
        memCpyBanked(bytes, pointer + offset, bank, count);
        for (i = 0; i < count; ++i) {
            if (offset + i < length)
                auditCheck(bytes[i] == fixtureByte(offset + i), "debug fixture payload changed");
            else
                auditCheck(bytes[i] == 0xa5, "debug loader copied bytes past EOF");
        }
    }
}

static void runDebugReadCase(unsigned int fileLength, unsigned int expectedLength)
{
    byte* allocation;
    byte bank, loadedBank = 0, canary;
    unsigned int bytesLoaded;
    writeDebugFixture("debug.bin", fileLength);
    seedLargeAllocation(&allocation, &bank);
    bytesLoaded = b5DebugFileRead("debug.bin", &loadedBank);
    auditCheck(loadedBank == bank, "debug loader did not reuse its owned test span");
    auditCheck(bytesLoaded == expectedLength, "debug loader reported the wrong byte count");
    checkDebugBytes(allocation, bank, expectedLength);
    memCpyBanked(&canary, allocation + LARGE_SIZE, bank, 1);
    auditCheck(canary == 0x5a, "debug loader wrote beyond its allocation capacity");
    auditCheck(remove("debug.bin") == 0, "debug fixture cleanup");
    auditCheck(b10BankedDealloc(allocation, bank), "debug fixture allocation cleanup");
}

void readerdebugAuditCases(void)
{
    auditOutput("DEBUG READER CASES; ");
    runDebugReadCase(0, 0);
    runDebugReadCase(513, 513);
    runDebugReadCase(533, 533);
    runDebugReadCase(LARGE_SIZE + 1, LARGE_SIZE);
}
