#include "audit.h"
#include "../../commands.h"
#include "../../logic.h"
#include "../../memoryManager.h"
#include "../../helpers.h"
#include <string.h>

extern LOGICEntry logics[];
extern LOGICFile logicFiles[];

static void runFalseIfSaidSkip(byte count)
{
    LOGICEntry entry, savedEntry;
    LOGICFile file, savedFile;
    byte header[5];
    byte closeIf[] = {0xff, 3, 0};
    byte falseLanding[] = {0xff, 0, 0};
    byte branch[] = {3, 250, 77, 3, 251, 88, 0};
    byte bank, *code;
    byte oldHigh;
    unsigned int closeAt = 5u + 2u * (unsigned int)count;
    /* A stale high byte lands on this sentinel and runs the false branch. */
    unsigned int falseCloseAt = closeAt + 512u;
    unsigned int codeSize = falseCloseAt + sizeof(falseLanding) + sizeof(branch);

    header[0] = 0xff;
    header[1] = 7;
    header[2] = 2;
    header[3] = 0x0e;
    header[4] = count;
    b5GetLogicEntry(&savedEntry, 250);
    b5GetLogicFile(&savedFile, 250);
    code = b10BankedAlloc(codeSize, &bank);
    auditCheck(code != NULL, "FALSE IF SCRIPT ALLOCATION FAILED");
    memsetBanked(code, 0xfc, codeSize, bank);
    memCpyBanked(code, header, bank, sizeof(header));
    memCpyBanked(code + closeAt, closeIf, bank, sizeof(closeIf));
    memCpyBanked(code + closeAt + sizeof(closeIf), branch, bank, sizeof(branch));
    memCpyBanked(code + falseCloseAt, falseLanding, bank, sizeof(falseLanding));
    memCpyBanked(code + falseCloseAt + sizeof(falseLanding), branch, bank, sizeof(branch));

    memset(&entry, 0, sizeof(entry));
    memset(&file, 0, sizeof(file));
    entry.loaded = TRUE;
    entry.data = &logicFiles[250];
    file.logicCode = code;
    file.codeBank = bank;
    file.codeSize = (word)codeSize;
    b5SetLogicEntry(&entry, 250);
    b5SetLogicFile(&file, 250);

    flag[2] = FALSE;
    var[250] = var[251] = 0;
    oldHigh = *(volatile byte*)0xf0;
    *(volatile byte*)0xf0 = 1;
    auditOutput("FALSE-IF SAID COUNT MUST IGNORE STALE HIGH BYTE; ");
    executeLogic(&logics[250], 250);
    *(volatile byte*)0xf0 = oldHigh;

    auditCheck(var[250] == 0, "FALSE IF SAID SCAN RAN THE IF BODY");
    auditCheck(var[251] == 88, "FALSE IF SAID SCAN MISSED THE FOLLOWING STATEMENT");
    b5SetLogicEntry(&savedEntry, 250);
    b5SetLogicFile(&savedFile, 250);
    auditCheck(b10BankedDealloc(code, bank), "FALSE IF SCRIPT LEAKED BYTECODE BUFFER");
}

static void runOrSaidSkip(byte count)
{
    LOGICEntry entry, savedEntry;
    LOGICFile file, savedFile;
    byte header[8];
    byte tail[] = {0xfc, 0xff, 0, 0, 3, 250, 77, 3, 251, 88, 0};
    byte bank, *code;
    unsigned int closeAt = 8u + 2u * (unsigned int)count;
    unsigned int codeSize = closeAt + sizeof(tail);

    header[0] = 0xff;
    header[1] = 7;
    header[2] = 2;
    header[3] = 0xfc;
    header[4] = 7;
    header[5] = 2;
    header[6] = 0x0e;
    header[7] = count;
    b5GetLogicEntry(&savedEntry, 250);
    b5GetLogicFile(&savedFile, 250);
    code = b10BankedAlloc(codeSize, &bank);
    auditCheck(code != NULL, "OR SAID SCRIPT ALLOCATION FAILED");
    memsetBanked(code, 0xfc, codeSize, bank);
    memCpyBanked(code, header, bank, sizeof(header));
    memCpyBanked(code + closeAt, tail, bank, sizeof(tail));

    memset(&entry, 0, sizeof(entry));
    memset(&file, 0, sizeof(file));
    entry.loaded = TRUE;
    entry.data = &logicFiles[250];
    file.logicCode = code;
    file.codeBank = bank;
    file.codeSize = (word)codeSize;
    b5SetLogicEntry(&entry, 250);
    b5SetLogicFile(&file, 250);

    flag[2] = TRUE;
    var[250] = var[251] = 0;
    executeLogic(&logics[250], 250);
    auditCheck(var[250] == 77 && var[251] == 88,
        "OR SAID SCAN DID NOT SKIP 255 ARGUMENT WORDS");
    b5SetLogicEntry(&savedEntry, 250);
    b5SetLogicFile(&savedFile, 250);
    auditCheck(b10BankedDealloc(code, bank), "OR SAID SCRIPT LEAKED BYTECODE BUFFER");
}

static void runFalseIfGotoIgnoresStaleHighByte(void)
{
    LOGICEntry entry, savedEntry;
    LOGICFile file, savedFile;
    static byte script[] = {
        0xff, 7, 2, 0xff, 2, 0,
        1, 250, 1, 251, 0
    };
    byte bank, *code;
    byte oldHigh;

    b5GetLogicEntry(&savedEntry, 250);
    b5GetLogicFile(&savedFile, 250);
    code = b10BankedAlloc(sizeof(script), &bank);
    auditCheck(code != NULL, "FALSE IF GOTO SCRIPT ALLOCATION FAILED");
    memCpyBanked(code, script, bank, sizeof(script));

    memset(&entry, 0, sizeof(entry));
    memset(&file, 0, sizeof(file));
    entry.loaded = TRUE;
    entry.data = &logicFiles[250];
    file.logicCode = code;
    file.codeBank = bank;
    file.codeSize = sizeof(script);
    b5SetLogicEntry(&entry, 250);
    b5SetLogicFile(&file, 250);

    flag[2] = FALSE;
    var[250] = var[251] = 0;
    oldHigh = *(volatile byte*)0xf2;
    *(volatile byte*)0xf2 = 1;
    executeLogic(&logics[250], 250);
    *(volatile byte*)0xf2 = oldHigh;

    auditCheck(var[250] == 0, "FALSE IF GOTO EXECUTED ITS SKIPPED COMMAND");
    auditCheck(var[251] == 1,
        "FALSE IF GOTO USED A STALE HIGH BYTE IN ITS DISPLACEMENT");
    b5SetLogicEntry(&savedEntry, 250);
    b5SetLogicFile(&savedFile, 250);
    auditCheck(b10BankedDealloc(code, bank), "FALSE IF GOTO SCRIPT LEAKED BYTECODE BUFFER");
}

void scriptstreamAuditCases(void)
{
    runFalseIfGotoIgnoresStaleHighByte();
    runFalseIfSaidSkip(1);
    runFalseIfSaidSkip(128);
    runFalseIfSaidSkip(255);
    runOrSaidSkip(255);
}
