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
    /* A stale second word must not satisfy a test beyond the current input. */
    script[2] = 3;
    script[5] = 0x21; script[6] = 0x43;
    script[7] = 0x0f; script[8] = 0x27;
    script[9] = 255; script[10] = 3; script[11] = 0;
    script[12] = 3; script[13] = 250; script[14] = 77; script[15] = 0;
    file.codeSize = 16;
    b5SetLogicFile(&file, 250);
    memCpyBanked(file.logicCode, script, bank, sizeof(script));
    numInputWords = 1;
    flag[2] = TRUE; flag[4] = FALSE; var[250] = 0;
    executeLogic(&logics[250], 250);
    auditCheck(!var[250] && !flag[4], "SAID MATCHED STALE WORDS PAST CURRENT INPUT COUNT");
    b5SetLogicEntry(&saved, 250);
    auditCheck(b10BankedDealloc(file.logicCode, bank), "SAID SCRIPT LEAKED BYTECODE BUFFER");
}
static void longSaidScript(void)
{
    byte head[3] = {255,14,128};
    byte tail[10] = {255,3,0,3,250,77,3,251,88,0};
    byte rest[2] = {0x0f,0x27};
    LOGICEntry entry, saved;
    LOGICFile file;
    byte bank;
    b5GetLogicEntry(&saved, 250);
    memset(&entry, 0, sizeof(entry)); memset(&file, 0, sizeof(file));
    file.logicCode = b10BankedAlloc(320, &bank);
    file.codeBank = bank; file.codeSize = 269;
    memsetBanked(file.logicCode, 0xff, 320, bank);
    memCpyBanked(file.logicCode, head, bank, sizeof(head));
    memCpyBanked(file.logicCode+259, tail, bank, sizeof(tail));
    entry.loaded = TRUE; entry.data = &logicFiles[250];
    b5SetLogicEntry(&entry, 250); b5SetLogicFile(&file, 250);
    numInputWords = 0; flag[2] = FALSE; flag[4] = FALSE;
    var[250] = var[251] = 0;
    auditOutput("128-WORD SAID MUST SKIP ALL ARGUMENT BYTES; ");
    executeLogic(&logics[250], 250);
    auditCheck(!var[250] && var[251] == 88, "SAID ARGUMENT BYTE COUNT WRAPPED AT 128 WORDS");
    memCpyBanked(file.logicCode+3, rest, bank, sizeof(rest));
    numInputWords = 1; inputWords[0] = 0x1234;
    flag[2] = TRUE; flag[4] = FALSE; var[250] = var[251] = 0;
    executeLogic(&logics[250], 250);
    auditCheck(var[250] == 77 && var[251] == 88 && flag[4], "LONG REST-OF-LINE MATCH LOST REMAINING ARGUMENT BYTES");
    b5SetLogicEntry(&saved, 250);
    auditCheck(b10BankedDealloc(file.logicCode, bank), "LONG SAID SCRIPT CLEANUP");
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
    auditCheck(b7GetInputWord(10) == -1 && b7GetInputWord(255) == -1,
        "OUT-OF-RANGE INPUT WORD ACCESS WAS NOT REJECTED");
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
    longSaidScript();
}
