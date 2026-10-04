#include "audit.h"
#include "../../words.h"
#include "../../memoryManager.h"
#include <stdio.h>
#include <string.h>

extern byte allocationArray[];

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

void readerwordsAuditCases(void)
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
}
