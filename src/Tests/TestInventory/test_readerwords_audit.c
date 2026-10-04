#include "audit.h"
#include "../../words.h"
#include "../../memoryManager.h"
#include <stdio.h>
#include <string.h>

extern byte allocationArray[];
extern char* wordsData;
extern byte wordsDataBank;

#pragma wrapped-call (push, trampoline, PALETTE_MANAGER_BANK)
static void readerwordsAuditCasesInBank(void);
#pragma wrapped-call (pop)

#pragma code-name (push, "BANKRAM0F")
#pragma rodata-name (push, "BANKRAM0F")

static void writeWordsFile(byte withSynonym)
{
    static const char word[] = "apple";
    static const char nextWord[] = "banana";
    static const byte start[] = {0, 2};
    static const byte firstSynonym[] = {0x12, 0x34};
    static const byte nextSynonym[] = {0x23, 0x45};
    FILE* file = fopen("words.tok", "wb");
    byte prefix = 0;
    byte encoded;
    int i;
    auditCheck(file != 0, "WORDS.TOK fixture creation");
    auditCheck(fwrite(start, 1, sizeof(start), file) == sizeof(start), "WORDS.TOK fixture header write");
    auditCheck(fwrite(&prefix, 1, 1, file) == 1, "WORDS.TOK first word prefix write");
    for (i = 0; i < sizeof(word) - 1; ++i) {
        encoded = (byte)(word[i] ^ 0x7f);
        if (i == sizeof(word) - 2) encoded |= 0x80;
        auditCheck(fwrite(&encoded, 1, 1, file) == 1, "WORDS.TOK encoded word write");
    }
    if (withSynonym)
    {
        auditCheck(fwrite(firstSynonym, 1, sizeof(firstSynonym), file) == sizeof(firstSynonym), "WORDS.TOK synonym write");
        auditCheck(fwrite(&prefix, 1, 1, file) == 1, "WORDS.TOK second word prefix write");
        for (i = 0; i < sizeof(nextWord) - 1; ++i) {
            encoded = (byte)(nextWord[i] ^ 0x7f);
            if (i == sizeof(nextWord) - 2) encoded |= 0x80;
            auditCheck(fwrite(&encoded, 1, 1, file) == 1, "WORDS.TOK second encoded word write");
        }
        auditCheck(fwrite(nextSynonym, 1, sizeof(nextSynonym), file) == sizeof(nextSynonym), "WORDS.TOK second synonym write");
    }
    auditCheck(fclose(file) == 0, "WORDS.TOK fixture close");
}

static void checkAllocationBaseline(const byte* baseline, const char* message)
{
    byte current;
    int i;
    for (i = 0; i < ALLOCATION_ARRAY_SIZE; ++i) {
        memCpyBanked(&current, allocationArray + i, MEMORY_MANAGEMENT_BANK, 1);
        auditCheck(current == baseline[i], message);
    }
}

static int dictionaryWordCount(void)
{
    int count;
    memCpyBanked((byte*)&count, (byte*)&numWords, WORD_BANK, sizeof(count));
    return count;
}

static int dictionarySynonymCount(void)
{
    int count;
    memCpyBanked((byte*)&count, (byte*)&numSynonyms, WORD_BANK, sizeof(count));
    return count;
}

static char* seedWordsDataEndCanary(byte* bank);

static void makeIndexedWord(char* word, byte wordLength, int index, char first)
{
    byte letter, identityLength = wordLength < 5 ? wordLength - 1 : 4;
    int value = index;
    for (letter = 0; letter < wordLength; ++letter) word[letter] = 'x';
    word[0] = first;
    for (letter = identityLength; letter > 0; --letter)
    {
        word[letter] = (char)('a' + value % 26);
        value /= 26;
    }
    word[wordLength] = 0;
}

static void writeIndexedWordsFile(int count, byte wordLength, char first, byte sharedPrefix, char* word)
{
    static const byte start[] = {0, 2};
    FILE* file = fopen("words.tok", "wb");
    byte prefix = 0;
    int index, letter;
    auditCheck(file != 0, "indexed WORDS.TOK fixture creation");
    auditCheck(fwrite(start, 1, sizeof(start), file) == sizeof(start), "indexed WORDS.TOK header write");
    for (index = 0; index < count; ++index)
    {
        makeIndexedWord(word, wordLength, index, first);
        for (letter = 0; letter < wordLength; ++letter)
            word[letter] ^= 0x7f;
        word[wordLength - 1] |= 0x80;
        prefix = index ? sharedPrefix : 0;
        auditCheck(fwrite(&prefix, 1, 1, file) == 1, "indexed WORDS.TOK prefix write");
        auditCheck(fwrite(word, 1, wordLength, file) == wordLength, "indexed WORDS.TOK word write");
        auditCheck(fputc(index >> 8, file) != EOF && fputc(index, file) != EOF,
            "indexed WORDS.TOK synonym write");
    }
    auditCheck(fclose(file) == 0, "indexed WORDS.TOK fixture close");
}

static void dictionaryMetadataCapacity(const byte* allocationBaseline, char* query)
{
    byte dataBank, canary, loadedBank;
    char* guardedAllocation, *loadedAllocation;
    auditOutput("WORDS.TOK 1500-ENTRY METADATA BOUNDARY; ");
    writeIndexedWordsFile(1500, 4, 'w', 0, query);
    b12LoadWords();
    makeIndexedWord(query, 4, 1499, 'w');
    auditCheck(dictionaryWordCount() == 1500 && dictionarySynonymCount() == 1499,
        "1500-entry WORDS.TOK boundary count changed");
    auditCheck(b12FindSynonymNum(query, RAM_BANK) == 1499,
        "1500th WORDS.TOK entry was not searchable");
    b12DiscardWords();
    auditCheck(dictionaryWordCount() == 0 && dictionarySynonymCount() == 0,
        "1500-entry dictionary discard left published state");
    checkAllocationBaseline(allocationBaseline, "1500-entry dictionary allocations were not released");
    auditCheck(remove("words.tok") == 0, "1500-entry WORDS.TOK fixture cleanup");

    auditOutput("WORDS.TOK 1501-ENTRY METADATA OVERFLOW; ");
    writeIndexedWordsFile(1501, 4, 'w', 0, query);
    b12LoadWords();
    auditCheck(dictionaryWordCount() == 0 && dictionarySynonymCount() == 0,
        "1501-entry WORDS.TOK metadata overflow was published");
    makeIndexedWord(query, 4, 1500, 'w');
    auditCheck(b12FindSynonymNum(query, RAM_BANK) == -1,
        "1501-entry WORDS.TOK overflow remained searchable");
    b12DiscardWords();
    auditCheck(dictionaryWordCount() == 0 && dictionarySynonymCount() == 0,
        "rejected 1501-entry dictionary discard changed state");
    checkAllocationBaseline(allocationBaseline, "rejected 1501-entry dictionary leaked allocations");
    auditCheck(remove("words.tok") == 0, "1501-entry WORDS.TOK fixture cleanup");

    auditOutput("WORDS.TOK 8241-BYTE DATA ALLOCATION OVERFLOW; ");
    guardedAllocation = seedWordsDataEndCanary(&dataBank);
    writeIndexedWordsFile(201, 40, 'w', 0, query);
    b12LoadWords();
    if (dictionaryWordCount() != 0)
    {
        memCpyBanked((byte*)&loadedAllocation, (byte*)&wordsData, WORD_BANK, sizeof(loadedAllocation));
        memCpyBanked(&loadedBank, &wordsDataBank, WORD_BANK, sizeof(loadedBank));
        auditCheck(loadedAllocation == guardedAllocation && loadedBank == dataBank,
            "WORDS.TOK end canary does not guard the actual data allocation");
    }
    memCpyBanked(&canary, (byte*)guardedAllocation + LARGE_SIZE, dataBank, 1);
    auditCheck(canary == 0x5a,
        "8241-byte WORDS.TOK data crossed its 8000-byte allocation end");
    makeIndexedWord(query, 40, 200, 'w');
    auditCheck(dictionaryWordCount() == 0 && dictionarySynonymCount() == 0,
        "8241-byte WORDS.TOK data overflow was published");
    auditCheck(b12FindSynonymNum(query, RAM_BANK) == -1,
        "8241-byte WORDS.TOK data overflow remained searchable");
    b12DiscardWords();
    b12DiscardWords();
    checkAllocationBaseline(allocationBaseline, "rejected oversized WORDS.TOK data leaked allocations");
    auditCheck(remove("words.tok") == 0, "oversized WORDS.TOK fixture cleanup");

    auditOutput("WORDS.TOK INVALID PREFIX; ");
    writeIndexedWordsFile(2, 3, 'c', 4, query);
    b12LoadWords();
    auditCheck(dictionaryWordCount() == 0 && dictionarySynonymCount() == 0,
        "WORDS.TOK prefix longer than the previous word was published");
    makeIndexedWord(query, 3, 0, 'c');
    auditCheck(b12FindSynonymNum(query, RAM_BANK) == -1,
        "WORDS.TOK invalid-prefix word remained searchable");
    b12DiscardWords();
    b12DiscardWords();
    checkAllocationBaseline(allocationBaseline, "rejected invalid-prefix WORDS.TOK leaked allocations");
    auditCheck(remove("words.tok") == 0, "invalid-prefix WORDS.TOK fixture cleanup");
}

static char* seedWordsDataEndCanary(byte* bank)
{
    byte canary = 0x5a;
    char* allocation = (char*)b10BankedAlloc(LARGE_SIZE, bank);
    auditCheck((unsigned int)allocation + LARGE_SIZE <= BANK_MAX,
        "WORDS.TOK allocation has no bank-local end canary");
    memCpyBanked((byte*)allocation + LARGE_SIZE, &canary, *bank, 1);
    auditCheck(b10BankedDealloc((byte*)allocation, *bank), "WORDS.TOK canary allocation release");
    return allocation;
}

static void readerwordsAuditCasesInBank(void)
{
    byte allocationBaseline[ALLOCATION_ARRAY_SIZE];
    char query[MAX_WORD_SIZE];
    int synonym;
    auditOutput("WORDS.TOK COMPLETENESS CASES; ");
    memset(query, 0, sizeof(query));
    strcpy(query, "apple");
    memCpyBanked(allocationBaseline, allocationArray, MEMORY_MANAGEMENT_BANK, sizeof(allocationBaseline));

    writeWordsFile(TRUE);
    b12LoadWords();
    auditCheck(dictionaryWordCount() == 2, "complete WORDS.TOK entries were rejected");
    auditCheck(b12FindSynonymNum(query, RAM_BANK) == 0x1234, "complete WORDS.TOK synonym was not searchable");
    b12DiscardWords();
    b12DiscardWords();
    auditCheck(dictionaryWordCount() == 0 && dictionarySynonymCount() == 0, "repeated dictionary discard left published state");
    checkAllocationBaseline(allocationBaseline, "complete WORDS.TOK allocations were not released");
    auditCheck(remove("words.tok") == 0, "complete WORDS.TOK fixture cleanup");

    writeWordsFile(FALSE);
    b12LoadWords();
    synonym = b12FindSynonymNum(query, RAM_BANK);
    auditCheck(dictionaryWordCount() == 0 && synonym == -1, "truncated WORDS.TOK entry was published without its synonym");
    b12DiscardWords();
    b12DiscardWords();
    auditCheck(dictionaryWordCount() == 0 && dictionarySynonymCount() == 0, "truncated dictionary discard left published state");
    checkAllocationBaseline(allocationBaseline, "truncated WORDS.TOK entry leaked allocations");
    auditCheck(remove("words.tok") == 0, "truncated WORDS.TOK fixture cleanup");

    dictionaryMetadataCapacity(allocationBaseline, query);
}

#pragma rodata-name (pop)
#pragma code-name (pop)

void readerwordsAuditCases(void)
{
    readerwordsAuditCasesInBank();
}
