#include "audit.h"
#include "../../parser.h"
#include "../../words.h"
#include "../../commands.h"
#include "../../logic.h"
#include <string.h>
#include <stdio.h>
extern char wordsTextStart[];
extern LOGICEntry logics[];
extern LOGICFile logicFiles[];
static unsigned long cycles(void)
{
    byte a = *(volatile byte*)0x9fb8;
    byte b = *(volatile byte*)0x9fb9;
    byte c = *(volatile byte*)0x9fba;
    byte d = *(volatile byte*)0x9fbb;
    return ((unsigned long)d << 24) | ((unsigned long)c << 16) | ((unsigned)b << 8) | a;
}
static void saidScript(void)
{
    /* if (said(0x1234, REST_OF_LINE)) assignn(v250, 77); return; */
    static byte script[64] = {255,14,2,0x34,0x12,0x0f,0x27,255,3,0,3,250,77,0};
    LOGICEntry entry, saved;
    LOGICFile file;
    byte bank, caller = RAM_BANK;
    b5GetLogicEntry(&saved, 250);
    memset(&entry, 0, sizeof(entry));
    memset(&file, 0, sizeof(file));
    file.logicCode = b10BankedAlloc(sizeof(script), &bank);
    file.codeBank = bank;
    file.codeSize = 14;
    memCpyBanked(file.logicCode, script, bank, sizeof(script));
    entry.loaded = TRUE;
    entry.data = &logicFiles[250];
    b5SetLogicEntry(&entry, 250);
    b5SetLogicFile(&file, 250);
    inputWords[0] = 0x1234;
    inputWords[1] = 0x4321;
    numInputWords = 2;
    flag[2] = TRUE; flag[4] = FALSE; var[250] = 0;
    auditOutput("REST-OF-LINE BYTECODE MUST MATCH; ");
    executeLogic(&logics[250], 250);
    auditCheck(var[250] == 77 && flag[4], "REST-OF-LINE SAID REJECTED MATCH OR SKIPPED WRONG BYTECODE");
    auditCheck(RAM_BANK == caller, "SAID SCRIPT LOST CALLER BANK");
    b5SetLogicEntry(&saved, 250);
    auditCheck(b10BankedDealloc(file.logicCode, bank), "SAID SCRIPT LEAKED BYTECODE BUFFER");
}

void interpreter20AuditCases(void)
{
    int synonym = 0x1234;
    int count = 0;
    unsigned long start, elapsed;
    char measured[64];
    char query[MAX_WORD_SIZE];
    memset(query, 0, sizeof(query));
    strcpy(query, "ant");
    memCpyBanked((byte*)inputWords, (byte*)&synonym, 0, sizeof(synonym));
    auditCheck(b7GetInputWord(0) == synonym,
        "PARSER TRUNCATED A 16-BIT SYNONYM NUMBER");
    memCpyBanked((byte*)&numWords, (byte*)&count, WORD_BANK, sizeof(count));
    auditOutput("EMPTY DICTIONARY SEARCH MUST RETURN; ");
    auditCheck(b12FindSynonymNum(query, RAM_BANK) == -1,
        "EMPTY DICTIONARY SEARCH DID NOT REPORT MISSING WORD");
    count = 1;
    memCpyBanked((byte*)&numWords, (byte*)&count, WORD_BANK, sizeof(count));
    memCpyBanked((byte*)wordsTextStart, (byte*)"bee", WORD_BANK, 3);
    auditOutput("BELOW-FIRST DICTIONARY SEARCH MUST RETURN; ");
    start = cycles();
    auditCheck(b12FindSynonymNum(query, RAM_BANK) == -1,
        "BELOW-FIRST DICTIONARY SEARCH DID NOT REPORT MISSING WORD");
    elapsed = cycles() - start;
    sprintf(measured, "BELOW-FIRST SEARCH CYCLES: %lu; ", elapsed);
    auditOutput(measured);
    saidScript();
}
