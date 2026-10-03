#include "audit.h"
#include "../../parser.h"
#include "../../words.h"
#include "../../commands.h"
#include "../../logic.h"
#include <string.h>
#include <stdio.h>
extern char wordsTextStart[];
extern char* wordsData;
extern byte wordsDataBank;
extern char** wordPointers;
extern byte wordsPointersBank;
extern word* synonymsList;
extern byte synonymsListBank;
extern byte allocationArray[];
extern char b7CurrentInputStr[];
#pragma wrapped-call (push, trampoline, PARSER_BANK)
extern void b7StripExtraChars(char* input);
#pragma wrapped-call (pop)
static void parserDictionary(void)
{
    byte* data;
    byte* pointers;
    byte* synonyms;
    byte dataBank, pointersBank, synBank;
    int count = 1, synonym = 777;
    char input[41];
    byte prefix[3] = {'b',0,0}, guard[8];
    data = b10BankedAlloc(100, &dataBank);
    pointers = b10BankedAlloc(100, &pointersBank);
    synonyms = b10BankedAlloc(100, &synBank);
    memCpyBanked(data, (byte*)"bee", dataBank, 4);
    memCpyBanked(pointers, (byte*)&data, pointersBank, sizeof(data));
    memCpyBanked(synonyms, (byte*)&synonym, synBank, sizeof(synonym));
    memCpyBanked((byte*)&wordsData, (byte*)&data, WORD_BANK, sizeof(data));
    memCpyBanked((byte*)&wordsDataBank, &dataBank, WORD_BANK, sizeof(dataBank));
    memCpyBanked((byte*)&wordPointers, (byte*)&pointers, WORD_BANK, sizeof(pointers));
    memCpyBanked((byte*)&wordsPointersBank, &pointersBank, WORD_BANK, sizeof(pointersBank));
    memCpyBanked((byte*)&synonymsList, (byte*)&synonyms, WORD_BANK, sizeof(synonyms));
    memCpyBanked((byte*)&synonymsListBank, &synBank, WORD_BANK, sizeof(synBank));
    memCpyBanked((byte*)&numWords, (byte*)&count, WORD_BANK, sizeof(count));
    memCpyBanked((byte*)wordsTextStart, (byte*)"bee", WORD_BANK, 3);
    strcpy(input, "bee!");
    var[9] = 0;
    b7LookupWords(input);
    auditCheck(numInputWords == 1 && inputWords[0] == synonym && !var[9],
        "PARSER REINTRODUCED PUNCTUATION AFTER NORMALIZING INPUT");
    memCpyBanked(data, prefix, dataBank, sizeof(prefix));
    memCpyBanked((byte*)wordsTextStart, prefix, WORD_BANK, sizeof(prefix));
    strcpy(input, "b bee");
    var[9] = 0;
    b7LookupWords(input);
    auditCheck(numInputWords == 1 && inputWords[0] == synonym && var[9] == 2,
        "PARSER FAILED A SHORT DICTIONARY WORD BEFORE A SPACE");
    memsetBanked(b7CurrentInputStr, 0xa5, sizeof(guard), PARSER_BANK);
    strcpy(input, "b b b b b b b b b b b b b b b b b b b b");
    b7LookupWords(input);
    memCpyBanked(guard, (byte*)b7CurrentInputStr, PARSER_BANK, sizeof(guard));
    auditCheck(guard[0] == 0xa5 && guard[1] == 0xa5 && numInputWords <= 10,
        "PARSER OVERWROTE INPUT BUFFER AFTER TEN RECOGNIZED WORDS");
    memsetBanked(b7CurrentInputStr, 0, sizeof(guard), PARSER_BANK);
    b12DiscardWords();
}
#pragma wrapped-call (push, trampoline, VIEW_CODE_BANK_2)
extern void bAFollowEgo(ViewTable* table);
#pragma wrapped-call (pop)
static void followCurrentView(void)
{
    static byte ids[] = {0, 251, 252};
    View original[3], views[3];
    ViewTable ego, oldEgo, follower;
    Loop loop;
    Cel cel;
    byte i, celBanks[3];
    byte* cells[3];
    memset(&ego, 0, sizeof(ego));
    memset(&follower, 0, sizeof(follower));
    memset(views, 0, sizeof(views));
    getViewTab(&oldEgo, 0);
    for (i=0; i<3; ++i) {
        getLoadedView(&original[i], ids[i]);
        memset(&loop, 0, sizeof(loop));
        memset(&cel, 0, sizeof(cel));
        cel.width = i == 1 ? 20 : 4;
        cel.height = 1;
        cells[i] = b10BankedAlloc(sizeof(cel), &celBanks[i]);
        memCpyBanked(cells[i], (byte*)&cel, celBanks[i], sizeof(cel));
        loop.cels = (Cel*)cells[i]; loop.celsBank = celBanks[i]; loop.numberOfCels = 1;
        views[i].loops = (Loop*)b10BankedAlloc(sizeof(loop), &views[i].loopsBank);
        memCpyBanked((byte*)views[i].loops, (byte*)&loop, views[i].loopsBank, sizeof(loop));
        views[i].loaded = TRUE; views[i].numberOfLoops = 1;
        setLoadedView(&views[i], ids[i]);
    }
    ego.currentView = 251; ego.xPos = 10; ego.yPos = 20;
    setViewTab(&ego, 0);
    follower.currentView = 252; follower.xPos = 18; follower.yPos = 20;
    follower.motion = FOLLOW; follower.param1 = 1; follower.param2 = 249;
    follower.param3 = 255; follower.direction = 5;
    flag[249] = FALSE;
    bAFollowEgo(&follower);
    auditCheck(follower.direction == 0 && follower.motion == NORMAL_MOTION && flag[249],
        "FOLLOW EGO USED VIEW ZERO WIDTH INSTEAD OF EGO CURRENT VIEW");
    setViewTab(&oldEgo, 0);
    for (i=0; i<3; ++i) {
        setLoadedView(&original[i], ids[i]);
        auditCheck(b10BankedDealloc(cells[i], celBanks[i]), "FOLLOW FIXTURE CEL CLEANUP");
        auditCheck(b10BankedDealloc((byte*)views[i].loops, views[i].loopsBank), "FOLLOW FIXTURE LOOP CLEANUP");
    }
}
static void dictionaryLifetime(void)
{
    byte before[ALLOCATION_ARRAY_SIZE], after[ALLOCATION_ARRAY_SIZE];
    byte *data, *pointers, *synonyms;
    byte dataBank, pointersBank, synBank;
    memCpyBanked(before, allocationArray, MEMORY_MANAGEMENT_BANK, sizeof(before));
    data = b10BankedAlloc(500, &dataBank);
    pointers = b10BankedAlloc(1000, &pointersBank);
    synonyms = b10BankedAlloc(100, &synBank);
    memCpyBanked((byte*)&wordsData, (byte*)&data, WORD_BANK, sizeof(data));
    memCpyBanked((byte*)&wordsDataBank, &dataBank, WORD_BANK, sizeof(dataBank));
    memCpyBanked((byte*)&wordPointers, (byte*)&pointers, WORD_BANK, sizeof(pointers));
    memCpyBanked((byte*)&wordsPointersBank, &pointersBank, WORD_BANK, sizeof(pointersBank));
    memCpyBanked((byte*)&synonymsList, (byte*)&synonyms, WORD_BANK, sizeof(synonyms));
    memCpyBanked((byte*)&synonymsListBank, &synBank, WORD_BANK, sizeof(synBank));
    b12DiscardWords();
    memCpyBanked(after, allocationArray, MEMORY_MANAGEMENT_BANK, sizeof(after));
    auditCheck(!memcmp(before, after, sizeof(before)), "DICTIONARY DISCARD LEAKED POINTER OR SYNONYM ALLOCATIONS");
    b12DiscardWords();
    memCpyBanked(after, allocationArray, MEMORY_MANAGEMENT_BANK, sizeof(after));
    auditCheck(!memcmp(before, after, sizeof(before)), "REPEATED DICTIONARY DISCARD CHANGED ALLOCATION OWNERSHIP");
}

void runtime20AuditCases(void)
{
    dictionaryLifetime();
    followCurrentView();
    parserDictionary();
}
